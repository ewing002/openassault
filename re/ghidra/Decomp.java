import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class Decomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String out = args[0];
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        PrintWriter pw = new PrintWriter(new FileWriter(out));
        for (int i = 1; i < args.length; i++) {
            boolean containing = args[i].startsWith("c");
            Address a = toAddr(Long.parseLong(containing ? args[i].substring(1) : args[i], 16));
            disassemble(a);
            Function f = containing ? getFunctionContaining(a) : getFunctionAt(a);
            if (f == null) f = createFunction(a, "f_" + args[i]);
            if (f == null) { pw.println("// no function at " + args[i]); continue; }
            DecompileResults r = ifc.decompileFunction(f, 120, monitor);
            pw.println("// ===== " + args[i] + " entry " + f.getEntryPoint());
            pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : ("// failed: " + r.getErrorMessage()));
        }
        pw.close();
    }
}
