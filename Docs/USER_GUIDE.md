# User guide

DaliCandy is one plug-in. Put it on an instrument track and play / sequence it with MIDI; put it on an audio track (or enable its audio input) and it processes and analyses that audio while adding candy on top. Dry level, sidechain analysis source, capture and live negative space only have an effect while an audio input is connected.

The header holds the preset browser (prev / next / menu / search / save / favourite / random), the five page tabs and the output meter. The footer has Out gain, Candy level (and Dry level in the FX), Quality (Eco / Standard / High), status, CPU, voices and the output-protection counter.

## 1 Generate
Seven macros (defaults Complexity 65, Motion 75, Space 45, Density 38, Chaos 40, Brightness 50, Musicality 65), Category (8), Length (Short / Medium / Long / Custom ms), Root / Scale, Negative-space sensitivity. **GENERATE** runs on a worker thread and returns 8 diverse candidates (3x oversampled, rejected for silence / clipping / DC / wrong length, scored, then picked for diversity; the taste model biases the choice). Click a card to make it current; *Use*, *Breed* (current x card), *Slot* (store in one of 8 sound slots = MIDI channel in the Instrument / per-event sound in the sequencer), *>* audition. Drag a waveform out to the DAW or file manager to drop a real WAV.
Shape the sound: **Mutate / Evolve** (amount knob), **Recall** (steps back through history), Audition, Like / Favourite / Dislike (teaches the local taste model), *New Ln* regenerates one layer, *Lock Ln* protects a layer from Mutate / Evolve / Regenerate. Export row: one-shot, loop, multi-bar, per-layer, dry/wet/FX-only, all candidates, MIDI; 44.1/48/88.2/96 kHz, 16 / 24 / 32f; "DRAG WAV" drags the current sound.

## 2 Sequence
Mode (Sparse, Groove Follower, Call & Response, Controlled Chaos, Phrase Evolution, Euclidean, Probability, Transition Builder), bars 1-32, division 1/4..1/64 with triplet / dotted, swing, probability, ratchet, micro-timing, velocity + range, pitch variation, event length, Euclid steps / hits / rotate, evolve amount, freeze. The pattern view shows events (height = velocity, dim = probabilistic); **click an event to freeze it** - frozen events survive *New pattern* and *Evolve phrase*. Sequencer positions come from the host PPQ; *Free run* plays without transport.

## 3 Synthesis
Layer tabs (4), active / mute / solo / lock, engine choice, 16 named engine parameters (right-click a knob: lock the parameter or randomise it), envelope, pitch, pan, level, per-layer insert effect + sends, and the shared FX bus (delay, reverb, chorus/flanger/phaser, width, master gain, low cut). Bottom: rendered preview and 8-band spectrum of the sound.

## 4 Modulation
32 routes (source, destination, bipolar depth, curve, slew). Modulator editor: LFOs (7 shapes incl. 16-point custom curve, tempo sync), MSEG (drag points, shift-drag bends a segment), S&H, chaos, random walk, 16-step modulator, follower. Four host-automatable macros feed the matrix.

## 5 Track analysis
Arm **Capture** (4 / 8 / 16 / 32 bars; needs the audio input or sidechain to be connected, i.e. effect use); recording starts at the next bar line while the host plays. **Import WAV** works in every mode (the only option on an instrument track). Result: waveform, bar grid, section estimates, per-step *opportunity map*, 1/16-grid placement suggestions with score breakdown. Click the map to add your own hit; accept / reject / lock suggestions; **Apply to sequence** turns accepted ones into events. The masking panel compares candy vs track in 8 bands (an estimate, not a guarantee). The live panel shows level, bands, brightness, onsets, stereo correlation; *Live negative space* lets the FX leave out non-frozen events when the incoming audio is busy; *Adaptive mix* scales the conservative low-cut / dynamic-EQ / width / trim adaptation.

## Presets
`.dalicandy` JSON, versioned (current v2; v1 prototype files migrate on load; files from a newer version are refused with a message). Save writes to the user bank; factory presets are read-only.
