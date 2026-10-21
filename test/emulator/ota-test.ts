// Boots the emulator firmware (pio run -e esp32dev-qemu) in Espressif's QEMU and tests OTA
// updates without a network: the emulator build accepts an image over the serial port
// (`ota-begin`, `ota-data`, `ota-end`) and writes it with the same code as a download. Checks that
// unsigned, wrongly signed and corrupted images are rejected, that a signed image boots from the
// other slot on probation, and that it rolls back because it never reaches a server.
//
//   bun ota-test.ts <qemu-system-xtensa> <firmware.factory.bin> <firmware.bin> <work dir>
import { createHash, createPrivateKey, sign } from "node:crypto";

const [qemuPath, factoryPath, appPath, workDir] = Bun.argv.slice(2);
const PORT = 5598;
const BOOT_TIMEOUT_MS = 180_000;
const KEYS = `${import.meta.dir}/keys`;

let failures = 0;
function check(name: string, ok: boolean, detail?: unknown) {
	console.log(`${ok ? "PASS" : "FAIL"}  ${name}${ok || detail === undefined ? "" : ` -> ${JSON.stringify(detail)}`}`);
	if (!ok) failures++;
}
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

const testKey = createPrivateKey(await Bun.file(`${KEYS}/emulator-test.key.pem`).text());
const otherKey = createPrivateKey(await Bun.file(`${KEYS}/emulator-other.key.pem`).text());
const sha256 = (data: Uint8Array) => createHash("sha256").update(data).digest("hex");
const signHex = (data: Uint8Array, key: ReturnType<typeof createPrivateKey>) => sign("sha256", data, key).toString("hex");

// --- 4 MB flash image ----------------------------------------------------------------------------
const flash = new Uint8Array(4 * 1024 * 1024).fill(0xff);
flash.set(new Uint8Array(await Bun.file(factoryPath).arrayBuffer()), 0);
const flashPath = `${workDir}/flash-ota.bin`;
await Bun.write(flashPath, flash);

// --- QEMU, and the serial port over TCP with backpressure for the large uploads ---------------------
let transcript = "";
let drained: (() => void) | null = null;
let qemu: ReturnType<typeof Bun.spawn>;
let socket: Awaited<ReturnType<typeof Bun.connect>> | null = null;

async function startQemu() {
	qemu = Bun.spawn(
		[
			qemuPath,
			"-nographic",
			"-machine",
			"esp32",
			"-drive",
			`file=${flashPath},if=mtd,format=raw`,
			"-serial",
			`tcp:127.0.0.1:${PORT},server,wait=on`,
			"-monitor",
			"none",
			"-global",
			"driver=timer.esp32.timg,property=wdt_disable,value=true",
		],
		{ stdout: "ignore", stderr: "inherit" },
	);

	socket = null;
	for (let attempt = 0; attempt < 50 && !socket; attempt++) {
		try {
			socket = await Bun.connect({
				hostname: "127.0.0.1",
				port: PORT,
				socket: {
					data(_s, data) {
						transcript += new TextDecoder().decode(data);
					},
					drain() {
						drained?.();
					},
				},
			});
		} catch {
			await sleep(200);
		}
	}
	if (!socket) throw new Error("QEMU serial port didn't open");
}

/**
 * Restarts the emulated chip from power-on. After the bignum accelerator was used (the ECDSA check),
 * a software restart crashes in QEMU (a stale interrupt hits the task watchdog's handler on the next
 * boot); real chips restart fine. So the test power-cycles whenever the device restarts; the flash
 * file keeps what was written.
 */
async function powerCycle(afterText: string, timeoutMs: number, from: number, settleMs = 500) {
	const found = (await waitFor(afterText, timeoutMs, from)) >= 0;
	await sleep(settleMs);
	socket?.end();
	qemu.kill();
	await qemu.exited;
	transcript += "\n--- power cycle ---\n";
	await startQemu();
	return found;
}

await startQemu();

/**
 * Writes to the serial port, followed by empty lines (which the console ignores): QEMU's UART only
 * hands received bytes to the firmware in blocks of its FIFO threshold, so the end of a long line
 * would otherwise wait for more input. A real UART flushes on its idle timeout.
 */
async function writeAll(text: string) {
	let data = new TextEncoder().encode(`${text}${"\n".repeat(128)}`);
	while (data.length > 0) {
		const written = socket!.write(data);
		data = data.subarray(Math.max(written, 0));
		if (data.length > 0) await new Promise<void>((resolve) => (drained = resolve));
	}
}

async function waitFor(text: string, timeoutMs: number, from = 0) {
	const start = Date.now();
	while (Date.now() - start < timeoutMs) {
		const index = transcript.indexOf(text, from);
		if (index >= 0) return index;
		await sleep(100);
	}
	return -1;
}

async function command(line: string, expect: string, timeoutMs = 10_000) {
	const from = transcript.length;
	await writeAll(`${line}\n`);
	return (await waitFor(expect, timeoutMs, from)) >= 0;
}

async function status() {
	const from = transcript.length;
	await writeAll("status\n");
	await waitFor("updates:", 10_000, from);
	return transcript.slice(from);
}

/** Uploads an image; resolves with the device's answer to `ota-end`. */
async function upload(image: Uint8Array, declaredSha: string, signature?: string) {
	if (!(await command(`ota-begin ${image.length} ${declaredSha}${signature ? ` ${signature}` : ""}`, "OTA ready", 60_000))) {
		return "begin failed";
	}

	// 200 bytes per line. The device acknowledges every 2000 bytes ("OTA ok <written>"): the
	// emulated UART has no flow control and drops what doesn't fit into its buffer.
	const CHUNK = 200;
	const ACK_EVERY = 2000;
	for (let offset = 0; offset < image.length; offset += ACK_EVERY) {
		const end = Math.min(offset + ACK_EVERY, image.length);
		let batch = "";
		for (let i = offset; i < end; i += CHUNK) {
			batch += `ota-data ${Buffer.from(image.subarray(i, Math.min(i + CHUNK, end))).toString("hex")}\n`;
		}
		const from = transcript.length;
		await writeAll(batch);
		if ((await waitFor(`OTA ok ${end}\n`, 30_000, from)) < 0) {
			const error = transcript.slice(from).match(/OTA error: [^\r\n]*/);
			return error ? error[0] : `no acknowledgement at ${end} bytes`;
		}
	}

	const from = transcript.length;
	await writeAll("ota-end\n");
	const start = Date.now();
	while (Date.now() - start < 600_000) {
		const answer = transcript.slice(from).match(/OTA (installed|error: [^\r\n]*)/);
		if (answer) return answer[0];
		await sleep(200);
	}
	return "no answer";
}

try {
	check("firmware boots", (await waitFor("Waiting for Wi-Fi settings", BOOT_TIMEOUT_MS)) >= 0);
	await sleep(1500);
	const before = await status();
	check("runs from app0 and only accepts signed updates", before.includes("partition: app0") && before.includes("updates:   signed only"), before);

	// A small fake image (it starts with the ESP image magic byte, which Update checks).
	const fake = new Uint8Array(8192).map((_, i) => (i * 7) & 0xff);
	fake[0] = 0xe9;

	check(
		"unsigned image is refused",
		await command(`ota-begin ${fake.length} ${sha256(fake)}`, "OTA error: This firmware only installs signed updates"),
	);
	const wrongKey = await upload(fake, sha256(fake), signHex(fake, otherKey));
	check("image signed with another key is rejected", wrongKey === "OTA error: The update's signature is invalid", wrongKey);

	const corrupted = fake.slice();
	corrupted[100] ^= 0xff;
	const corruptedResult = await upload(corrupted, sha256(fake), signHex(fake, testKey));
	check(
		"corrupted image is rejected by its SHA-256",
		corruptedResult === "OTA error: The download doesn't match its SHA-256",
		corruptedResult,
	);
	const stillOld = await status();
	check("rejected updates leave the firmware untouched", stillOld.includes("partition: app0") && !stillOld.includes("probation"), stillOld);

	// --- A real, signed update ---------------------------------------------------------------------
	const app = new Uint8Array(await Bun.file(appPath).arrayBuffer());
	const started = Date.now();
	const result = await upload(app, sha256(app), signHex(app, testKey));
	check(`signed image (${app.length} bytes) is installed`, result === "OTA installed", result);
	console.log(`      upload took ${Math.round((Date.now() - started) / 1000)} s`);

	// Power-cycle instead of the device's own restart (it has written everything by now; a warm
	// restart would crash in QEMU and the bootloader would roll the update back).
	await powerCycle("OTA installed", 1000, transcript.lastIndexOf("OTA installed"), 20);
	const bootFrom = transcript.length;
	check("restarts into the new slot", (await waitFor("partition app1", BOOT_TIMEOUT_MS, bootFrom)) >= 0);
	check("new firmware is on probation", (await waitFor("New firmware on probation", 20_000, bootFrom)) >= 0);
	await sleep(1500);
	const probation = await status();
	check("status shows the probation", probation.includes("partition: app1") && probation.includes("on probation"), probation);

	// The emulator can't reach a server, so the probation (20 s in emulator builds) runs out.
	const probationFrom = transcript.length;
	const timedOut = await waitFor("didn't reach the server in time", 60_000, probationFrom);
	check("rolls back when it doesn't reach the server", timedOut >= 0);
	await powerCycle("rst:0xc", 30_000, Math.max(timedOut, probationFrom));
	const rollbackFrom = transcript.length;
	check("restarts from the previous slot", (await waitFor("partition app0", BOOT_TIMEOUT_MS, rollbackFrom)) >= 0);
	check("reports the failed update", (await waitFor("The update to emulator failed", 20_000, rollbackFrom)) >= 0);
	await sleep(1500);
	const after = await status();
	check(
		"status after the rollback",
		after.includes("partition: app0") && after.includes("the update to emulator failed") && !after.includes("probation"),
		after,
	);
} catch (error) {
	check("test ran to the end", false, String(error));
} finally {
	await Bun.write(`${workDir}/ota-transcript.log`, transcript);
	socket?.end();
	qemu.kill();
}

if (failures > 0) {
	const printable = transcript
		.split("\n")
		.filter((line) => !line.startsWith("OTA ok"))
		.join("\n")
		.replace(/[^\x09\x0a\x20-\x7e]/g, "");
	console.log("\n--- last 80 lines of serial output ---");
	console.log(printable.split("\n").slice(-80).join("\n"));
}

console.log(failures === 0 ? "\nALL OTA CHECKS PASSED" : `\n${failures} OTA CHECK(S) FAILED`);
process.exit(failures === 0 ? 0 : 1);
