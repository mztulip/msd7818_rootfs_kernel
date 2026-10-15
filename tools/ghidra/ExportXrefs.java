// List references to given addresses and decompile the referencing functions.
// Script args: <out_file> <hex_addr> [<hex_addr> ...]
//@category MStar

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

public class ExportXrefs extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] a = getScriptArgs();
		Set<Function> funcs = new LinkedHashSet<>();
		try (PrintWriter w = new PrintWriter(new File(a[0]))) {
			for (int i = 1; i < a.length; i++) {
				for (Reference r : getReferencesTo(toAddr(Long.parseLong(a[i], 16)))) {
					Function f = getFunctionContaining(r.getFromAddress());
					w.printf("// ref %s <- %s %s (%s)%n", a[i], r.getFromAddress(),
						f == null ? "?" : f.getName(), r.getReferenceType());
					if (f != null)
						funcs.add(f);
				}
			}
			DecompInterface dec = new DecompInterface();
			dec.openProgram(currentProgram);
			for (Function f : funcs) {
				DecompileResults r = dec.decompileFunction(f, 120, monitor);
				w.printf("// ==== %s @ %s%n", f.getName(), f.getEntryPoint());
				w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// failed");
			}
		}
		println("functions: " + funcs.size());
	}
}
