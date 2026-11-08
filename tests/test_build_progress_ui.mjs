import assert from 'node:assert/strict';
import {durationText, stepMarkup, progressSummary} from '../assets/dashboard/build-progress.js';
assert.equal(durationText(undefined), 'Timing not recorded');
assert.equal(durationText(65), '1m 5s');
const markup = stepMarkup({number:9,run_id:'manual9'}, {id:'test',label:'<script>bad</script>',status:'skipped'});
assert.ok(markup.includes('&lt;script&gt;'));
assert.ok(!markup.includes('<script>'));
assert.ok(markup.includes('Timing not recorded'));
assert.equal(progressSummary({counts:{passed:2,running:1,pending:3,failed:0,skipped:1}}),
  '2 passed · 1 running · 3 pending · 0 failed · 1 skipped');
console.log('Progress UI formatting and escaping checks passed');
