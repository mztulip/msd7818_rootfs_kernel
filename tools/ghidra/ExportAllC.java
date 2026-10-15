// Decompile every function of the program into one C file (for grep).
// Script args: <out_file>
//@category MStar

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

import java.io.File;
import java.io.PrintWriter;

public class ExportAllC extends GhidraScript {
	@Override
	protected void run() throws Exception {
		DecompInterface dec = new DecompInterface();
		dec.openProgram(currentProgram);
		int n = 0;
		try (PrintWriter w = new PrintWriter(new File(getScriptArgs()[0]))) {
			for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
				if (monitor.isCancelled())
					break;
				DecompileResults r = dec.decompileFunction(f, 60, monitor);
				w.printf("// ==== %s @ %s%n", f.getName(), f.getEntryPoint());
				w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// failed");
				n++;
			}
		}
		println("decompiled " + n);
	}
}
