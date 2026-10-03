// Export Ghidra's analysis of the current program to plain text files.
//
// Usage (headless): -postScript ExportAnalysis.java <output-dir> [crt-start-address]
//
// Functions below crt-start-address are tagged "game", the rest "crt" (the
// statically linked C runtime).
//
// Writes:
//   functions.tsv     one row per function: address, name, size, region, source, callers, callees
//   decompiled.c      decompiler output for every non-thunk function, in address order
//   decompiled-game.c same, game functions only
//   imports.tsv     imported DLL functions and who calls them
//   strings.tsv     defined strings and the functions that reference them
//
// @category Export

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolType;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;

public class ExportAnalysis extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        File outDir = new File(args.length > 0 ? args[0] : "analysis");
        outDir.mkdirs();
        if (args.length > 1) {
            crtStart = toAddr(args[1]);
        }

        exportFunctions(new File(outDir, "functions.tsv"));
        exportDecompiled(new File(outDir, "decompiled.c"), new File(outDir, "decompiled-game.c"));
        exportImports(new File(outDir, "imports.tsv"));
        exportStrings(new File(outDir, "strings.tsv"));
    }

    private Address crtStart;

    private String region(Function f) {
        if (crtStart == null) {
            return "";
        }
        return f.getEntryPoint().compareTo(crtStart) < 0 ? "game" : "crt";
    }

    private List<Function> functionsInOrder() {
        List<Function> out = new ArrayList<>();
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext()) {
            out.add(it.next());
        }
        return out;
    }

    private static String names(Set<Function> fns) {
        Set<String> s = new TreeSet<>();
        for (Function f : fns) {
            s.add(f.getName());
        }
        return String.join(",", s);
    }

    private void exportFunctions(File file) throws Exception {
        try (PrintWriter w = new PrintWriter(file)) {
            w.println("address\tname\tsize\tregion\tsource\tthunk\tcallers\tcallees");
            for (Function f : functionsInOrder()) {
                w.printf("%s\t%s\t%d\t%s\t%s\t%s\t%s\t%s%n",
                    f.getEntryPoint(),
                    f.getName(),
                    f.getBody().getNumAddresses(),
                    region(f),
                    f.getSymbol().getSource(),
                    f.isThunk() ? "thunk" : "",
                    names(f.getCallingFunctions(monitor)),
                    names(f.getCalledFunctions(monitor)));
            }
        }
    }

    private void exportDecompiled(File file, File gameFile) throws Exception {
        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        decomp.openProgram(currentProgram);
        try (PrintWriter w = new PrintWriter(file); PrintWriter g = new PrintWriter(gameFile)) {
            for (Function f : functionsInOrder()) {
                if (f.isThunk() || f.isExternal()) {
                    continue;
                }
                StringBuilder sb = new StringBuilder();
                sb.append(String.format("// ---- %s @ %s (%d bytes, %s)%n",
                    f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses(),
                    f.getSymbol().getSource()));
                DecompileResults r = decomp.decompileFunction(f, 60, monitor);
                if (r.decompileCompleted()) {
                    sb.append(r.getDecompiledFunction().getC()).append('\n');
                } else {
                    sb.append(String.format("// decompile failed: %s%n%n", r.getErrorMessage()));
                }
                w.print(sb);
                if (region(f).equals("game")) {
                    g.print(sb);
                }
            }
        } finally {
            decomp.dispose();
        }
    }

    private void exportImports(File file) throws Exception {
        try (PrintWriter w = new PrintWriter(file)) {
            w.println("library\tname\tcallers");
            for (Symbol s : currentProgram.getSymbolTable().getExternalSymbols()) {
                if (s.getSymbolType() != SymbolType.FUNCTION && s.getSymbolType() != SymbolType.LABEL) {
                    continue;
                }
                Set<Function> callers = new java.util.HashSet<>();
                // External symbol refs point at the IAT slot / thunk; follow both levels.
                for (Reference r : s.getReferences()) {
                    addRefOwner(callers, r.getFromAddress());
                    Function thunk = getFunctionAt(r.getFromAddress());
                    if (thunk != null) {
                        callers.addAll(thunk.getCallingFunctions(monitor));
                    }
                    for (Reference r2 : getReferencesTo(r.getFromAddress())) {
                        addRefOwner(callers, r2.getFromAddress());
                    }
                }
                w.printf("%s\t%s\t%s%n", s.getParentNamespace().getName(), s.getName(), names(callers));
            }
        }
    }

    private void addRefOwner(Set<Function> out, Address a) {
        Function f = getFunctionContaining(a);
        if (f != null && !f.isThunk()) {
            out.add(f);
        }
    }

    private void exportStrings(File file) throws Exception {
        try (PrintWriter w = new PrintWriter(file)) {
            w.println("address\tsection\tfunctions\tvalue");
            DataIterator it = currentProgram.getListing().getDefinedData(true);
            while (it.hasNext()) {
                Data d = it.next();
                if (!d.hasStringValue()) {
                    continue;
                }
                Set<Function> users = new java.util.HashSet<>();
                for (Reference r : getReferencesTo(d.getAddress())) {
                    addRefOwner(users, r.getFromAddress());
                }
                String block = currentProgram.getMemory().getBlock(d.getAddress()).getName();
                String value = String.valueOf(d.getValue())
                    .replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r");
                w.printf("%s\t%s\t%s\t%s%n", d.getAddress(), block, names(users), value);
            }
        }
    }
}
