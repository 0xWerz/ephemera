// Plays the stream the way a visitor would, in a Chrome nobody sees, and says what arrived.
//   bun probe.mjs http://windows-pc/ [seconds] [picture.png]
// It reports what a person would notice: how sharp (size, bitrate), how smooth (frames a second,
// how even), how late (network round trip, time waiting to be shown), and whether a key pressed
// in the browser reaches the game (Esc opens the game's menu; the second picture shows it).
import { chromium } from 'playwright-core';
import path from 'node:path';

const [url = 'http://windows-pc/', seconds = '12', picture = 'stream.png'] = process.argv.slice(2);
const scratch = process.env.PROBE_SCRATCH || path.resolve('.profile');
const browser = await chromium.launchPersistentContext(scratch, {
  executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  headless: true,
  viewport: { width: 1920, height: 1080 },
  // PROBE_RESOLVE=name=address: where this machine does not use the stream's DNS (a Tailscale name, with MagicDNS off here).
  args: ['--autoplay-policy=no-user-gesture-required', '--mute-audio', '--no-first-run', '--no-default-browser-check',
    ...(process.env.PROBE_RESOLVE ? [`--host-resolver-rules=MAP ${process.env.PROBE_RESOLVE.replace('=', ' ')}`] : [])],
});
const page = browser.pages()[0] || (await browser.newPage());
// Every connection the page opens is kept where its numbers can be read.
await page.addInitScript(() => {
  window.__peers = [];
  const Real = window.RTCPeerConnection;
  window.RTCPeerConnection = function (...args) {
    const peer = new Real(...args);
    window.__peers.push(peer);
    return peer;
  };
  window.RTCPeerConnection.prototype = Real.prototype;
  Object.assign(window.RTCPeerConnection, Real);
});
const said = [];
page.on('console', (message) => { if (said.length < 400) said.push(message.text()); });
await page.goto(`${url}?AutoConnect=true&AutoPlayVideo=true&StartVideoMuted=true&HoveringMouse=true&StreamerId=Ephemera`, { waitUntil: 'load', timeout: 30000 });

const read = () => page.evaluate(async () => {
  const out = { video: null, pair: null, codec: null, audio: null, ways: null };
  for (const peer of window.__peers || []) {
    const stats = await peer.getStats();
    const all = [...stats.values()];
    for (const s of all) {
      if (s.type === 'inbound-rtp' && s.kind === 'video') {
        out.video = { frames: s.framesDecoded, dropped: s.framesDropped, width: s.frameWidth, height: s.frameHeight, fps: s.framesPerSecond, bytes: s.bytesReceived,
          lost: s.packetsLost, jitter: s.jitter, decodeTime: s.totalDecodeTime, bufferDelay: s.jitterBufferDelay, bufferCount: s.jitterBufferEmittedCount,
          freezes: s.freezeCount, freezeTime: s.totalFreezesDuration, keyFrames: s.keyFramesDecoded, nacks: s.nackCount, plis: s.pliCount, decoder: s.decoderImplementation,
          betweenFrames: s.totalInterFrameDelay, betweenFramesSquared: s.totalSquaredInterFrameDelay };
        const codec = all.find((c) => c.id === s.codecId);
        out.codec = codec ? `${codec.mimeType} ${codec.sdpFmtpLine || ''}` : null;
      }
      if (s.type === 'inbound-rtp' && s.kind === 'audio') out.audio = { bytes: s.bytesReceived, level: s.audioLevel, energy: s.totalAudioEnergy };
      if (s.type === 'candidate-pair' && s.state === 'succeeded') {
        const far = all.find((c) => c.id === s.remoteCandidateId);
        (out.ways = out.ways || []).push(`${far ? far.address : '?'} ${far ? far.candidateType : ''} ${(s.currentRoundTripTime * 1000 || 0).toFixed(0)} ms${s.nominated ? ' (in use)' : ''}`);
      }
      if (s.type === 'candidate-pair' && s.nominated) {
        const local = all.find((c) => c.id === s.localCandidateId), remote = all.find((c) => c.id === s.remoteCandidateId);
        out.pair = { rtt: s.currentRoundTripTime, local: local && `${local.candidateType} ${local.protocol}`, remote: remote && `${remote.candidateType} ${remote.protocol} ${remote.address}` };
      }
    }
  }
  const video = document.querySelector('video');
  out.element = video ? { ready: video.readyState, width: video.videoWidth, height: video.videoHeight, paused: video.paused } : null;
  return out;
});

// Wait for a picture (the game may still be loading: up to two minutes).
let first = null;
for (let waited = 0; waited < 120; waited++) {
  first = await read();
  if (first.video && first.video.frames > 30) break;
  await page.waitForTimeout(1000);
}
if (!first.video || first.video.frames <= 30) {
  console.log(JSON.stringify({ ok: false, why: 'no picture arrived', state: first, page: said.slice(-25) }, null, 1));
  await page.screenshot({ path: picture });
  await browser.close();
  process.exit(1);
}
const samples = [];
let before = first, beforeAt = Date.now();
for (let i = 0; i < Number(seconds); i++) {
  await page.waitForTimeout(1000);
  const now = await read(), at = Date.now(), span = (at - beforeAt) / 1000;
  samples.push({ fps: (now.video.frames - before.video.frames) / span, mbps: (now.video.bytes - before.video.bytes) * 8 / 1e6 / span });
  before = now; beforeAt = at;
}
await page.screenshot({ path: picture });
// PROBE_PAIR=1: a second picture a third of a second later. With the player standing still the two should be
// the same picture; what differs between them is what crawls (the game's own shimmer plus the video coding's).
if (process.env.PROBE_PAIR) {
  await page.waitForTimeout(330);
  await page.screenshot({ path: picture.replace(/\.png$/, '_b.png') });
}
// A key from the browser: Esc is the game's menu. (PROBE_NOKEY=1 leaves the game alone: somebody may be playing.)
if (!process.env.PROBE_NOKEY) {
  await page.mouse.click(960, 540);
  await page.keyboard.press('Escape');
  await page.waitForTimeout(1500);
  await page.screenshot({ path: picture.replace(/\.png$/, '_key.png') });
  await page.keyboard.press('Escape');
}
// Sound: what actually arrives, measured on the track itself for four seconds (a muted page still receives it).
const sound = await page.evaluate(async () => {
  const tracks = [...document.querySelectorAll('video,audio')].map((e) => e.srcObject).filter(Boolean).flatMap((s) => s.getAudioTracks());
  if (!tracks.length) return { tracks: 0 };
  const context = new AudioContext();
  await context.resume();
  const analyser = context.createAnalyser();
  analyser.fftSize = 2048;
  context.createMediaStreamSource(new MediaStream([tracks[0]])).connect(analyser);
  const block = new Float32Array(analyser.fftSize);
  let peak = 0, sum = 0, count = 0;
  for (let i = 0; i < 40; i++) {
    await new Promise((done) => setTimeout(done, 100));
    analyser.getFloatTimeDomainData(block);
    for (const value of block) { sum += value * value; count++; peak = Math.max(peak, Math.abs(value)); }
  }
  return { tracks: tracks.length, context: context.state, loudnessDb: +(20 * Math.log10(Math.sqrt(sum / count) + 1e-9)).toFixed(1), peakDb: +(20 * Math.log10(peak + 1e-9)).toFixed(1) };
});
before = await read();
const v = before.video, n = v.frames - first.video.frames;
const meanGap = (v.betweenFrames - first.video.betweenFrames) / Math.max(n, 1);
const gapSpread = Math.sqrt(Math.max((v.betweenFramesSquared - first.video.betweenFramesSquared) / Math.max(n, 1) - meanGap * meanGap, 0));
const sorted = samples.map((s) => s.fps).sort((a, b) => a - b);
console.log(JSON.stringify({
  ok: true,
  picture: `${v.width}x${v.height}`, codec: before.codec, decoder: v.decoder,
  framesASecond: { usual: +sorted[sorted.length >> 1].toFixed(1), lowest: +sorted[0].toFixed(1), highest: +sorted[sorted.length - 1].toFixed(1) },
  megabitsASecond: +(samples.reduce((a, s) => a + s.mbps, 0) / samples.length).toFixed(1),
  betweenFramesMs: { usual: +(meanGap * 1000).toFixed(1), spread: +(gapSpread * 1000).toFixed(1) },
  networkRoundTripMs: before.pair && +(before.pair.rtt * 1000).toFixed(1),
  waitingToBeShownMs: +(1000 * (v.bufferDelay - first.video.bufferDelay) / Math.max(v.bufferCount - first.video.bufferCount, 1)).toFixed(1),
  decodeMs: +(1000 * (v.decodeTime - first.video.decodeTime) / Math.max(n, 1)).toFixed(1),
  lostPackets: v.lost, droppedFrames: v.dropped, freezes: v.freezes, route: before.pair, waysThatWork: before.ways, sound: { ...sound, received: before.audio },
}, null, 1));
await browser.close();
