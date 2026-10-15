// Decompile every function that references a string matching one of the
// given regexes (case-insensitive). Writes <out_dir>/strings_funcs.c and a
// list <out_dir>/strings_refs.txt. Used on the stock eCos firmware
// (binaries ... 8001C/decompressed.bin, MIPS LE, base 0x80000180).
// Script args: <out_dir> <regex> [<regex> ...]
//@category MStar

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.PrintWriter;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.regex.Pattern;

public class ExportByStrings extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		File out = new File(args[0]);
		out.mkdirs();
		Pattern[] pats = new Pattern[args.length - 1];
		for (int i = 1; i < args.length; i++)
			pats[i - 1] = Pattern.compile(args[i], Pattern.CASE_INSENSITIVE);

		Map<Function, StringBuilder> funcs = new LinkedHashMap<>();
		try (PrintWriter refs = new PrintWriter(new File(out, "strings_refs.txt"))) {
			DataIterator it = currentProgram.getListing().getDefinedData(true);
			while (it.hasNext() && !monitor.isCancelled()) {
				Data d = it.next();
				if (!d.hasStringValue())
					continue;
				String s = StringDataInstance.getStringDataInstance(d).getStringValue();
				if (s == null)
					continue;
				boolean hit = false;
				for (Pattern p : pats)
					if (p.matcher(s).find())
						hit = true;
				if (!hit)
					continue;
				for (Reference r : getReferencesTo(d.getAddress())) {
					Function f = getFunctionContaining(r.getFromAddress());
					refs.printf("%s %s <- %s %s%n", d.getAddress(), s.replace("\n", "\\n"),
						r.getFromAddress(), f == null ? "?" : f.getName() + "@" + f.getEntryPoint());
					if (f != null)
						funcs.computeIfAbsent(f, k -> new StringBuilder()).append(s.replace("\n", "\\n")).append(" | ");
				}
			}
		}

		DecompInterface dec = new DecompInterface();
		dec.openProgram(currentProgram);
		try (PrintWriter c = new PrintWriter(new File(out, "strings_funcs.c"))) {
			for (Map.Entry<Function, StringBuilder> e : funcs.entrySet()) {
				Function f = e.getKey();
				DecompileResults res = dec.decompileFunction(f, 120, monitor);
				c.printf("// ==== %s @ %s  strings: %s%n", f.getName(), f.getEntryPoint(), e.getValue());
				c.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "// decompile failed");
			}
		}
		println("functions: " + funcs.size());
	}
}
