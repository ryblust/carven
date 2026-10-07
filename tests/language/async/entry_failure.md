Exit status: 1. Stdout: empty.

Stderr must contain these fragments in this exact order (the report path may vary):
root failure selected

child cancellation requested: true

child closed

entry_failure.cv:11:1: error: failure 'tests.language.async.entry_failure.EntryFailure' escaped the program entry

  failure: EntryFailure {}

  note: program exited with a failure status



The unobserved lexical child receives cancellation intent on the parent's failure
edge, but never accepts it at a checkpoint. Its deferred continuation must run to
completion before the root's selected failure is reported. All markers use
stderr, so the harness can verify closure/report ordering within one stream.
The nominal failure remains the selected root outcome after closing.
