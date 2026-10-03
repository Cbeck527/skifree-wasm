// Parse a C header of recovered types into the program's data type manager.
//
// Usage (headless): -postScript ApplyTypes.java <types.h>
//
// Types are stored in the program so ApplySymbols.java signatures and globals
// can refer to them. Existing Windows types (HDC, RECT, ...) resolve against
// the program's data type manager. No preprocessor support: keep the header
// to plain typedefs, structs, and enums.
//
// @category Analysis

import ghidra.app.script.GhidraScript;
import ghidra.app.util.cparser.C.CParser;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeManager;

import java.io.File;
import java.nio.file.Files;
import java.util.Map;

public class ApplyTypes extends GhidraScript {

    @Override
    protected void run() throws Exception {
        File file = new File(getScriptArgs()[0]);
        String src = Files.readString(file.toPath());
        DataTypeManager dtm = currentProgram.getDataTypeManager();

        CParser parser = new CParser(dtm, true, new DataTypeManager[] { dtm });
        parser.parse(src);

        Map<String, DataType> composites = parser.getComposites();
        Map<String, DataType> enums = parser.getEnums();
        Map<String, DataType> typedefs = parser.getTypes();
        println(String.format("ApplyTypes: %d structs, %d enums, %d typedefs from %s",
            composites.size(), enums.size(), typedefs.size(), file));
        String msgs = parser.getParseMessages();
        if (msgs != null && !msgs.isBlank()) {
            println("ApplyTypes parse messages:\n" + msgs);
        }
        for (DataType dt : composites.values()) {
            println("  " + dt.getPathName() + " (" + dt.getLength() + " bytes)");
        }
    }
}
