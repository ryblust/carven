Exit status: 1. Stdout: empty.

Stderr must contain these fragments in this exact order (the report path may vary):
root requested child cancellation

child accepts cancellation: true

entry_cancelled.cv:9:1: error: cancellation escaped the program entry

  note: program exited with a failure status



Stderr must not contain either unreachable marker or a nominal failure report.
The child accepts an explicit cancellation request at cancellation_point(). Its
cancelled completion propagates through the root's await, closes the root's
lexical scope, and produces the independent cancelled-entry report and status 1.
Calling cancel() alone would only request cancellation and could not satisfy this
case; the checkpoint must accept it. Neither source main takes host arguments.
