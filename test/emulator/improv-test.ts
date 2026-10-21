// Boots the emulator firmware (pio run -e esp32dev-qemu) in Espressif's QEMU and drives its serial
// port with the official Improv Wi-Fi Serial client (the package ESP Web Tools uses), then the text
// console. See README.md in this folder.
//
//   bun improv-test.ts <qemu-system-xtensa> <firmware.factory.bin> <work dir>
import { ImprovSerial } from "improv-wifi-serial-sdk/dist/serial.js";

const [qemuPath, factoryPath, workDir] = Bun.argv.slice(2);
const PORT = 5599;
// Boots take a few seconds locally but much longer on shared CI runners.
const BOOT_TIMEOUT_MS = 180_000;

let failures = 0;
function check(name: string, ok: boolean, detail?: unknown) {
	console.log(`${ok ? "PASS" : "FAIL"}  ${name}${ok || detail === undefined ? "" : ` -> ${JSON.stringify(detail)}`}`);
	if (!ok) failures++;
}
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

// --- 4 MB flash image ----------------------------------------------------------------------------
const factory = new Uint8Array(await Bun.file(factoryPath).arrayBuffer());
const flash = new Uint8Array(4 * 1024 * 1024).fill(0xff);
flash.set(factory, 0);
const flashPath = `${workDir}/flash-improv.bin`;
await Bun.write(flashPath, flash);

const qemu = Bun.spawn(
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
		// Like ESP-IDF's own QEMU runner: an emulated chip on a slow host would otherwise be reset by
		// the timer-group watchdogs during boot, over and over.
		"-global",
		"driver=timer.esp32.timg,property=wdt_disable,value=true",
	],
	{ stdout: "ignore", stderr: "inherit" },
);

// --- Serial port shim (Web Serial-like readable/writable over the TCP socket) ----------------------
let transcript = "";
let controller: ReadableStreamDefaultController<Uint8Array> | null = null;

let socket: Awaited<ReturnType<typeof Bun.connect>> | null = null;
for (let attempt = 0; attempt < 50 && !socket; attempt++) {
	try {
		socket = await Bun.connect({
			hostname: "127.0.0.1",
			port: PORT,
			socket: {
				data(_s, data) {
					const bytes = new Uint8Array(data);
					transcript += new TextDecoder().decode(bytes);
					try {
						controller?.enqueue(bytes);
					} catch {
						controller = null;
					}
				},
			},
		});
	} catch {
		await sleep(200);
	}
}
if (!socket) throw new Error("QEMU serial port didn't open");

const port = {
	get readable() {
		return new ReadableStream<Uint8Array>({
			start(c) {
				controller = c;
			},
			cancel() {
				controller = null;
			},
		});
	},
	writable: new WritableStream<Uint8Array>({
		write(chunk) {
			socket!.write(chunk);
		},
	}),
};

async function waitFor(text: string, timeoutMs: number, from = 0) {
	const start = Date.now();
	while (Date.now() - start < timeoutMs) {
		const index = transcript.indexOf(text, from);
		if (index >= 0) return index;
		await sleep(100);
	}
	return -1;
}

async function command(line: string, expect: string, timeoutMs = 8000) {
	const from = transcript.length;
	socket!.write(`${line}\n`);
	return (await waitFor(expect, timeoutMs, from)) >= 0;
}

/**
 * Right after a restart the emulated UART sometimes loses the first input on slow CI runners (it
 * made the v2.0.0-beta.1 tag build fail); repeat the harmless command a few times.
 */
async function commandAfterRestart(line: string, expect: string) {
	for (let attempt = 0; attempt < 3; attempt++) {
		if (await command(line, expect)) return true;
	}
	return false;
}

const logger = { log() {}, error() {}, debug() {} };

try {
	// --- Boot ----------------------------------------------------------------------------------
	check("firmware boots to the setup prompt", (await waitFor("Waiting for Wi-Fi settings", BOOT_TIMEOUT_MS)) >= 0);

	// --- Improv with the official client ------------------------------------------------------
	const improv = new ImprovSerial(port as any, logger as any);
	const info = await improv.initialize(10000);
	check("Improv detected; state is Authorized (ready for Wi-Fi)", improv.state === 2, improv.state);
	check(
		"device info: firmware, version, chip, name",
		info?.firmware === "NetIgnite Agent" && info.chipFamily === "ESP32" && /^NetIgnite-[0-9A-F]{6}$/.test(info.name) && info.version.startsWith("2.0.0"),
		info,
	);

	const networks = await improv.scan();
	check("Wi-Fi scan returns the (empty) list", Array.isArray(networks) && networks.length === 0, networks);

	let provisionError: unknown = null;
	try {
		await improv.provision("TestNet", "secret123", 40000);
	} catch (error) {
		provisionError = error;
	}
	check("provisioning fails cleanly with 'unable to connect'", provisionError !== null && improv.error === 3, {
		provisionError: String(provisionError),
		error: improv.error,
	});
	await sleep(500); // the state packet follows the error packet
	check("state returns to Authorized after the failure", improv.state === 2, improv.state);

	const state = await (improv as any).getNetworkState?.();
	if (state) check("network state: offline, supports Wi-Fi", state.online === false && state.supportsWifi === true, state);

	await improv.close();

	// --- Console --------------------------------------------------------------------------------
	check("help lists the commands", await command("help", "factory-reset confirm"));
	check("status before setup", await command("status", "server:    not set"));
	check("unknown command is reported", await command("frobnicate", 'Unknown command "frobnicate"'));
	check("invalid server URL is rejected", await command("server ftp://nope", "Invalid server URL"));
	check("non-token is rejected", await command("enroll hunter2", "isn't an enrollment token"));
	check(
		"setup stores server and token",
		await command("setup https://app.netignite.dev netignite_enroll_abc:def", "Saved. The agent enrolls with https://app.netignite.dev"),
	);
	check("server with IPv6 literal and port", await command("server http://[fd00::1]:18232", "Server set to http://[fd00::1]:18232"));
	check("wifi with a quoted SSID", await command('wifi "My Home Net" "pass word"', 'Connecting to "My Home Net"'));
	check("invalid static IP is rejected", await command("ip static 10.0.0.300 10.0.0.1 255.255.255.0", "isn't an IPv4 address"));
	check("name is saved", await command("name kitchen-esp", "Name saved"));
	check("log level", await command("log debug", "Log level: debug"));

	// The transcript only holds what the device sent: the token must never appear in it.
	check("enrollment token never printed", !transcript.includes("netignite_enroll_abc:def"));

	// --- Settings survive a restart ---------------------------------------------------------------
	const beforeReboot = transcript.length;
	socket.write("reboot\n");
	check("reboots", (await waitFor("I Device kitchen-esp", BOOT_TIMEOUT_MS, beforeReboot)) >= 0);
	await waitFor("[", 5000, transcript.length);
	await sleep(1500);
	const statusFrom = transcript.length;
	socket.write("status\n");
	await waitFor("partition:", 8000, statusFrom);
	const status = transcript.slice(statusFrom);
	check("server URL persisted", status.includes("server:    http://[fd00::1]:18232"), status);
	check("device name persisted", status.includes("device:    kitchen-esp"), status);
	check("waiting to enroll with the stored token", status.includes("agent:     waiting to enroll"), status);

	const improv2 = new ImprovSerial(port as any, logger as any);
	const info2 = await improv2.initialize(10000);
	check("Improv device name follows the console name", info2?.name === "kitchen-esp", info2);
	await improv2.close();

	check("factory reset erases everything", await command("factory-reset confirm", "All settings erased"));
	const afterReset = transcript.length;
	await waitFor("I Device NetIgnite-", BOOT_TIMEOUT_MS, afterReset);
	await sleep(1500);
	check("after the reset the agent is unconfigured", await commandAfterRestart("status", "server:    not set"));
} catch (error) {
	check("test ran to the end", false, String(error));
} finally {
	await Bun.write(`${workDir}/improv-transcript.log`, transcript);
	socket?.end();
	qemu.kill();
}

if (failures > 0) {
	// The device's serial output, so a CI log shows where it got stuck.
	const printable = transcript.replace(/[^\x09\x0a\x20-\x7e]/g, "");
	console.log("\n--- last 80 lines of serial output ---");
	console.log(printable.split("\n").slice(-80).join("\n"));
}

console.log(failures === 0 ? "\nALL EMULATOR CHECKS PASSED" : `\n${failures} EMULATOR CHECK(S) FAILED`);
process.exit(failures === 0 ? 0 : 1);
