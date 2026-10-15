// Ustawia punkty wejścia 8051 (reset i wektory przerwań zawierające LJMP), żeby analiza miała od czego zacząć.
// Użycie (headless): -preScript Seed8051.java
// @category MStar
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.SourceType;

public class Seed8051 extends GhidraScript {
	private static final int[] VECTORS = { 0x00, 0x03, 0x0B, 0x13, 0x1B, 0x23, 0x2B };
	private static final String[] NAMES = { "RESET", "INT0", "TIMER0", "INT1", "TIMER1", "SERIAL", "TIMER2" };

	@Override
	protected void run() throws Exception {
		for (int i = 0; i < VECTORS.length; i++) {
			Address a = toAddr(VECTORS[i]);
			// tylko wektory zaczynające się od LJMP (0x02)
			if ((getByte(a) & 0xFF) != 0x02) {
				continue;
			}
			new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
			createLabel(a, "VEC_" + NAMES[i], true, SourceType.USER_DEFINED);
			createFunction(a, "vec_" + NAMES[i].toLowerCase());
			println("seeded " + NAMES[i] + " @ " + a);
		}

		// Keil C51 dużo skacze pośrednio, więc analiza z wektorów pokrywa mało kodu.
		// Heurystyka: cele LCALL (0x12 hi lo) wskazywane z co najmniej 2 miejsc traktujemy jako funkcje.
		long size = currentProgram.getMemory().getBlocks()[0].getSize();
		byte[] code = new byte[(int) size];
		currentProgram.getMemory().getBytes(toAddr(0), code);
		java.util.Map<Integer, Integer> hits = new java.util.TreeMap<>();
		for (int i = 0; i + 2 < code.length; i++) {
			if ((code[i] & 0xFF) == 0x12) {
				int t = ((code[i + 1] & 0xFF) << 8) | (code[i + 2] & 0xFF);
				if (t >= 0x30 && t < code.length) {
					hits.merge(t, 1, Integer::sum);
				}
			}
		}
		int n = 0;
		for (java.util.Map.Entry<Integer, Integer> e : hits.entrySet()) {
			if (e.getValue() < 2) {
				continue;
			}
			Address a = toAddr(e.getKey());
			if (getInstructionAt(a) == null) {
				new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
			}
			if (getFunctionAt(a) == null && getInstructionAt(a) != null) {
				createFunction(a, null);
				n++;
			}
		}
		println("LCALL heuristic: " + n + " functions");
	}
}
