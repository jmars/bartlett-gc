# Bartlett Garbage Collector

A port and cleanup to modern C of the bartlett conservative mostly copying
garbage collector.

[The original paper](http://www.hpl.hp.com/techreports/Compaq-DEC/WRL-88-2.pdf)

## Novelties
* test program compiles using [cproc](https://github.com/michaelforney/cproc), a nice and minimal c compiler.
* the binary is only 20kb and does 10 million allocations in 400ms on my i7
* the entire thing is only ~350 lines of code
* this style of GC is used in javascriptcore