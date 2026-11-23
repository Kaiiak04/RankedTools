// Regenerate independently of the C++ implementation using pinned BeatLeader JS.
// node tests/generate_beatleader_pp_reference.mjs [path/to/rebalanced-pp.mjs]
import {readFile, writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';

const commit = '12415e959374f1813120fd67b9889ee3054e715e';
const sourceUrl = `https://raw.githubusercontent.com/BeatLeader/beatleader-website/${commit}/src/utils/beatleader/rebalanced-pp.js`;
let source;
if (process.argv[2]) source = await readFile(process.argv[2], 'utf8');
else {
    const response = await fetch(sourceUrl);
    if (!response.ok) throw new Error(`Upstream fetch failed: ${response.status}`);
    source = await response.text();
}
// Verify the upstream Git blob, including when using a local offline copy.
source = source.replace(/\r\n/g, '\n');
if (!source.endsWith('\n')) source += '\n';
const blob = createHash('sha1').update(`blob ${Buffer.byteLength(source)}\0`).update(source).digest('hex');
if (blob !== '57e47112c8cfc5ef862de879caf660eaed90b767') throw new Error(`Unexpected upstream blob: ${blob}`);
const {buildAccCurve, toStars, scorePp} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);

const base = {difficultyName: 'ExpertPlus', modeName: 'Standard', status: 3,
    predictedAcc: .98, sliderPercentage: 0, notes: 200, noteSpan: 120,
    accRating: 8, passRating: 8, techRating: 5};
const inputs = [];
const add = (label, overrides) => inputs.push({label, difficulty: {...base, ...overrides}});
for (const predictedAcc of [0, .6, .600001, .95, .955, .9573, .96, .965, .97, .9723, .975, .9777, .98, .985, .9873, .99, .995, .999499, .9995, 1]) {
    add(`prediction-${predictedAcc}`, {predictedAcc});
    add(`sliders-${predictedAcc}`, {predictedAcc, sliderPercentage: .8});
}
for (const notes of [0, 1, 19, 20, 21, 60, 114, 115, 116]) add(`notes-${notes}`, {notes});
for (const noteSpan of [0, 3, 11.999, 12, 12.001, 40, 74, 75, 76]) add(`span-${noteSpan}`, {noteSpan});
add('sparse-short', {notes: 5, noteSpan: 3, accRating: 1, passRating: 0, techRating: 0});
add('pass-only', {accRating: 0, techRating: 0});
add('tech-only', {accRating: 0, passRating: 0});
add('acc-only', {passRating: 0, techRating: 0});
for (const passRating of [12.999, 13, 13.001, 25]) add(`pass-${passRating}`, {passRating});
for (const techRating of [6.275399, 6.2754, 6.275401, 30]) add(`tech-${techRating}`, {techRating});

function ratings(d) {
    const curve = buildAccCurve(d.predictedAcc, d.sliderPercentage, d.notes, d.noteSpan);
    return {...d, curve, stars: toStars(d.accRating, d.passRating, d.techRating, curve, d.predictedAcc)};
}
// Exercise both sides of every star threshold without supplying the expected
// stars to the C++ calculation (it must derive them from the component ratings).
for (const threshold of [3.478, 4.2508, 5.3718, 6.3762, 7.1529, 8, 8.8584, 9.6894, 10.5]) {
    for (const offset of [-.00001, .00001]) {
        const target = threshold + offset;
        let lo = 0, hi = 30;
        for (let i = 0; i < 60; i++) {
            const mid = (lo + hi) / 2;
            if (ratings({...base, accRating: mid, passRating: 0, techRating: 0}).stars < target) lo = mid;
            else hi = mid;
        }
        add(`stars-${target}`, {accRating: (lo + hi) / 2, passRating: 0, techRating: 0});
    }
}

const accuracies = [.0001, .55, .6, .65, .7, .75, .8, .825, .85, .875, .9, .91, .92, .93, .9333459, .94,
    .95, .955, .9573, .96, .96321, .965, .97, .9725, .975, .9775, .98, .9825, .983, .983001, .985,
    .98767, .99, .990001, .9915, .995, .9975, .998, .999, 1];
const cases = inputs.map(({label, difficulty}) => {
    const r = ratings(difficulty);
    difficulty.stars = r.stars;
    const points = new Set(accuracies);
    // Include the mid-star bonus ramp, pass multiplier clamps and slider limits.
    for (const x of [difficulty.predictedAcc - .03, difficulty.predictedAcc - .015, difficulty.predictedAcc,
        difficulty.predictedAcc + .015,
        2 * difficulty.predictedAcc - 1 + Math.log(.8) / 6,
        2 * difficulty.predictedAcc - 1 + Math.log(3) / 6]) {
        for (const delta of [-.000001, 0, .000001]) if (x + delta > 0 && x + delta <= 1) points.add(x + delta);
    }
    return {label, difficulty, scores: [...points].sort((a, b) => a - b).map(x => [x, scorePp(x, r)[0]])};
});
const result = {source: `https://github.com/BeatLeader/beatleader-website/blob/${commit}/src/utils/beatleader/rebalanced-pp.js`,
    upstreamBlob: blob, version: 'PPv4.1', captured: '2026-11-05', cases};
// Keep each reference pair on one line so the data remains reviewable.
const json = JSON.stringify(result, null, 2).replace(/\[\n\s+([\d.eE+\-]+),\n\s+([\d.eE+\-]+)\n\s+\]/g, '[$1, $2]');
await writeFile(new URL('./fixtures/beatleader-ppv4.json', import.meta.url), json + '\n');
console.log(`Generated ${cases.reduce((n, c) => n + c.scores.length, 0)} PPv4.1 values across ${cases.length} charts.`);
