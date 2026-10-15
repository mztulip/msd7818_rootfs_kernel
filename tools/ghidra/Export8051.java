// Eksport analizy firmware'u 8051: dekompilacja wszystkich funkcji + mapa dostępów do XDATA.
// Użycie (headless): -postScript Export8051.java <katalog_wyjściowy>
// @category MStar
import java.io.File;
import java.io.PrintWriter;
import java.util.Map;
import java.util.TreeMap;
import java.util.TreeSet;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;

public class Export8051 extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		File out = new File(args.length > 0 ? args[0] : ".");
		out.mkdirs();

		DecompInterface dec = new DecompInterface();
		dec.openProgram(currentProgram);

		// XDATA (EXTMEM) adres -> funkcje, które go dotykają (R/W)
		Map<Long, TreeSet<String>> xdata = new TreeMap<>();

		try (PrintWriter c = new PrintWriter(new File(out, "decompiled.c"));
				PrintWriter fl = new PrintWriter(new File(out, "functions.txt"))) {
			for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
				fl.printf("%s %5d %s%n", f.getEntryPoint(), f.getBody().getNumAddresses(), f.getName());

				// MOV DPTR,#imm — 8051 adresuje XDATA przez DPTR, więc zbieramy stałe ładowane do DPTR
				for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
					for (Reference r : ins.getReferencesFrom()) {
						if (r.getToAddress().getAddressSpace().getName().equals("EXTMEM")) {
							String rw = r.getReferenceType().isWrite() ? "W" : (r.getReferenceType().isRead() ? "R" : "?");
							xdata.computeIfAbsent(r.getToAddress().getOffset(), k -> new TreeSet<>())
									.add(f.getName() + ":" + rw);
						}
					}
					if (ins.getMnemonicString().equals("MOV") && ins.getNumOperands() == 2
							&& ins.getDefaultOperandRepresentation(0).equals("DPTR")) {
						Object[] objs = ins.getOpObjects(1);
						if (objs.length == 1 && objs[0] instanceof Scalar) {
							long v = ((Scalar) objs[0]).getUnsignedValue();
							xdata.computeIfAbsent(v, k -> new TreeSet<>()).add(f.getName() + ":dptr");
						}
					}
				}

				DecompileResults res = dec.decompileFunction(f, 60, monitor);
				c.printf("// ===== %s @ %s =====%n", f.getName(), f.getEntryPoint());
				if (res != null && res.decompileCompleted()) {
					c.println(res.getDecompiledFunction().getC());
				} else {
					c.println("// decompilation failed");
				}
			}
		}

		try (PrintWriter x = new PrintWriter(new File(out, "xdata_refs.txt"))) {
			for (Map.Entry<Long, TreeSet<String>> e : xdata.entrySet()) {
				x.printf("%04X  %s%n", e.getKey(), String.join(" ", e.getValue()));
			}
		}
		println("Export done: " + out.getAbsolutePath());
	}
}
