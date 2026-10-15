// Decompile functions and everything they call, down to a given depth.
// Script args: <out_file> <depth> <hex_addr> [<hex_addr> ...]
// Example (stock firmware project binaries/ghidra_stock):
//   analyzeHeadless binaries/ghidra_stock stock -process decompressed.bin -noanalysis \
//     -scriptPath tools/ghidra -postScript ExportCallTree.java out.c 3 80005b80
//@category MStar

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayDeque;
import java.util.Deque;
import java.util.LinkedHashSet;
import java.util.Set;

public class ExportCallTree extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] a = getScriptArgs();
		int depth = Integer.parseInt(a[1]);
		Set<Function> seen = new LinkedHashSet<>();
		Deque<Object[]> q = new ArrayDeque<>();
		for (int i = 2; i < a.length; i++) {
			Function f = getFunctionAt(toAddr(Long.parseLong(a[i], 16)));
			if (f == null)
				f = createFunction(toAddr(Long.parseLong(a[i], 16)), null);
			if (f != null)
				q.add(new Object[] { f, 0 });
		}
		while (!q.isEmpty()) {
			Object[] e = q.poll();
			Function f = (Function) e[0];
			int d = (Integer) e[1];
			if (!seen.add(f) || d >= depth)
				continue;
			for (Function c : f.getCalledFunctions(monitor))
				q.add(new Object[] { c, d + 1 });
		}
		DecompInterface dec = new DecompInterface();
		dec.openProgram(currentProgram);
		try (PrintWriter w = new PrintWriter(new File(a[0]))) {
			for (Function f : seen) {
				DecompileResults r = dec.decompileFunction(f, 120, monitor);
				w.printf("// ==== %s @ %s%n", f.getName(), f.getEntryPoint());
				w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// failed");
			}
		}
		println("exported " + seen.size());
	}
}
