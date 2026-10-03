// Apply hand-maintained names, signatures, and comments from a TSV file.
//
// Usage (headless): -postScript ApplySymbols.java <symbols.tsv>
//
// Columns (tab-separated, '#' starts a comment line):
//   address   kind   name   [signature]   [comment]
//
//   kind = func  -> function at address (created if missing); signature is an
//                   optional C prototype, e.g. "int __stdcall WinMain(HINSTANCE,HINSTANCE,char *,int)"
//   kind = data  -> global label at address; signature is an optional C type, e.g. "int" or "char[32]"
//
// @category Analysis

import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.cparser.C.CParserUtils;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.FunctionDefinitionDataType;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import ghidra.util.data.DataTypeParser;

import java.io.File;
import java.nio.file.Files;
import java.util.List;

public class ApplySymbols extends GhidraScript {

    @Override
    protected void run() throws Exception {
        File file = new File(getScriptArgs()[0]);
        List<String> lines = Files.readAllLines(file.toPath());
        DataTypeManager dtm = currentProgram.getDataTypeManager();
        int n = 0;
        for (String line : lines) {
            if (line.isBlank() || line.startsWith("#")) {
                continue;
            }
            String[] c = line.split("\t", -1);
            Address addr = toAddr(c[0].trim());
            String kind = c[1].trim();
            String name = c[2].trim();
            String sig = c.length > 3 ? c[3].trim() : "";
            String comment = c.length > 4 ? c[4].trim() : "";

            if (kind.equals("func")) {
                Function f = getFunctionAt(addr);
                if (f == null) {
                    disassemble(addr);
                    f = createFunction(addr, name);
                }
                if (f == null) {
                    printerr("could not create function at " + addr);
                    continue;
                }
                f.setName(name, SourceType.USER_DEFINED);
                if (!sig.isEmpty()) {
                    FunctionDefinitionDataType def = CParserUtils.parseSignature(
                        (ghidra.app.services.DataTypeManagerService) null, currentProgram, sig, false);
                    if (def == null) {
                        printerr("could not parse signature for " + name + ": " + sig);
                    } else {
                        new ApplyFunctionSignatureCmd(addr, def, SourceType.USER_DEFINED)
                            .applyTo(currentProgram, monitor);
                    }
                }
                if (!comment.isEmpty()) {
                    f.setComment(comment);
                }
            } else if (kind.equals("data")) {
                createLabel(addr, name, true, SourceType.USER_DEFINED);
                if (!sig.isEmpty()) {
                    DataType dt = new DataTypeParser(dtm, dtm, null, DataTypeParser.AllowedDataTypes.ALL)
                        .parse(sig);
                    clearListing(addr, addr.add(dt.getLength() - 1));
                    createData(addr, dt);
                }
                if (!comment.isEmpty()) {
                    setPlateComment(addr, comment);
                }
            } else {
                printerr("unknown kind '" + kind + "' for " + addr);
                continue;
            }
            n++;
        }
        println("ApplySymbols: applied " + n + " entries from " + file);
    }
}
