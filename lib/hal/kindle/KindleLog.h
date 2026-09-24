#pragma once

// Where the Kindle build's diagnostics go, and why they stop growing.
//
// Everything diagnostic in this port already writes to stderr: the LOG_ macros
// through the Arduino shim's Serial, the [kindle] lines through fprintf, and
// the crash handler through write(2) on the raw descriptor. Keeping one
// destination is the point, because splitting them means reading a fault in
// one file and what led to it in another.
//
// So this takes ownership of that descriptor instead of adding a second sink:
// begin() points fd 2 at a file on the card, and every existing call site
// follows it without being touched.
//
// The rotation is the reason this exists at all. The launcher already keeps
// one generation per run, which bounds nothing WITHIN a run: a loop that logs
// on every iteration fills the card, and a reader is expected to run for hours.
// A byte cap and one generation back bounds it at twice the cap, always.

namespace KindleLog {

// Point stderr at the log file, rotating first if the previous run left one at
// the cap. Safe to call when the path is unwritable: stderr is left alone and
// the launcher's own capture still gets everything.
void begin();

// Rotate when the file has passed the cap. Called from the log sink rather than
// on a timer, so a quiet reader never pays for it.
void rotateIfNeeded();

}  // namespace KindleLog
