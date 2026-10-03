// Create functions in .text gaps that auto-analysis left undefined.
//
// Functions only reached through pointers (window procedures, timer callbacks,
// jump tables) are sometimes missed. Walk .text, and wherever an address is not
// inside a function and is not alignment padding (NOP/INT3), disassemble and
// create a function there. MSVC's multi-byte padding idioms (mov edi,edi;
// lea ecx,[ecx]; ...) placed before jump tables are skipped too.
//
// @category Analysis

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;

public class CreateMissingFunctions extends GhidraScript {

    @Override
    protected void run() throws Exception {
        MemoryBlock text = currentProgram.getMemory().getBlock(".text");
        Address a = text.getStart();
        Address end = text.getEnd();
        int created = 0;
        while (a.compareTo(end) < 0) {
            Function f = getFunctionContaining(a);
            if (f != null) {
                a = f.getBody().getMaxAddress().next();
                continue;
            }
            Data d = getDataContaining(a);
            if (d != null) {
                // Jump tables and other data embedded in code.
                a = d.getMaxAddress().next();
                continue;
            }
            int b = getByte(a) & 0xff;
            if (b == 0x90 || b == 0xcc || b == 0x00) {
                a = a.next();
                continue;
            }
            int pad = paddingLength(a);
            if (pad > 0) {
                a = a.add(pad);
                continue;
            }
            if (getInstructionAt(a) == null) {
                new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
            }
            Function nf = getInstructionAt(a) != null ? createFunction(a, null) : null;
            if (nf != null) {
                println("created function at " + a);
                created++;
                a = nf.getBody().getMaxAddress().next();
            } else {
                a = a.next();
            }
        }
        println("CreateMissingFunctions: created " + created + " functions");
    }

    // MSVC alignment no-ops: mov edi,edi / lea ecx,[ecx] / lea esp,[esp] /
    // lea ebx,[ebx] / lea esp,[esp+0] (disp32).
    private static final int[][] PADDING = {
        {0x8b, 0xff},
        {0x8d, 0x49, 0x00},
        {0x8d, 0x64, 0x24, 0x00},
        {0x8d, 0x9b, 0x00, 0x00, 0x00, 0x00},
        {0x8d, 0xa4, 0x24, 0x00, 0x00, 0x00, 0x00},
    };

    private int paddingLength(Address a) throws Exception {
        for (int[] p : PADDING) {
            boolean match = true;
            for (int i = 0; i < p.length && match; i++) {
                match = (getByte(a.add(i)) & 0xff) == p[i];
            }
            if (match) {
                return p.length;
            }
        }
        return 0;
    }
}
