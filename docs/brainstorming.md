# Brainstorming
This is where some concepts and ideas that might be added to Alta are kept

## Range Safety for Pointers
Basically, this means that a certain number of bytes after that start of a pointer
are guaranteed to be accessible and usable by the receiver of the pointer. This
would enable much safer interaction with C code in Alta, as well as enable more 
safety for code that can't make the switch to references (or smart pointers, when 
those are added).

Range safety would be guaranteed by the runtime, but, as much as possible, static
analysis will also be done at compile time in order to warn developers of possible
errors sooner rather than later.

You may be thinking "gee, this sure sounds *a lot* like C99's variable-sized 
arrays", and you'd be right. The key differences, however, are that
  * these can be attached on-the-fly to any random pointer via a type cast,
  * ~~they don't allocate anything, they simply describe the data they point to,~~ and
  * they're monitored for safety at both compile-time *and* runtime

### Syntax

My current favorite possible syntax for it is:
```alta
let foo: ptr[80] byte
# which also makes sense because pointers can be accessed via subscript
# so the syntax would remind you that you're working with pointers
#
# when compounded:
let foo: ptr[3] ptr[7] byte
```

Another possiblity that reuses existing syntax via attributes is:
```alta
let foo: @safe(80) ptr byte
# which, when compounded would look like this:
let foo: @safe(3) ptr @safe(7) ptr byte
```
But, in my opinion, that's uglier, and syntax should try to be as sweet as possible

## Try Catch Resume
Typical exception mechanisms do a pretty good job of throwing and catching errors.
However, the usual purpose of errors is to alert the user that something is wrong, right?
But, if the user can fix the problem, code should be able to continue without issue.

Therefore, if we can provide a mechanism to facilitate error recovery, it should make code much more robust.

### Syntax

Error throwing:
```alta
function something(): void {
  # oh no, something's wrong
  throw new WeirdError("oh no")
  # only resume *if* a condition is true
  # because, obviously, you threw the error
  # for a reason. now, in order to resume,
  # you need to make sure the error has been
  # fixed
  resume if errorFixedCondition
  printf("you fixed it!\n")
}
```

Error handling:
```alta
try {
  something()
  printf("this will happen :)\n")
} catch e: WeirdError {
  printf("it looks like there's a weird error. now fixing...\n")
  # somehow handle it and fix it
  # ...
  # now that we've fixed it, go back
  # to the function call
  resume
} catch e: OtherError {
  # it's also possible that there's an error we
  # can't recover from
  # in that case, just don't resume and log the
  # error or do whatever it is you want with it
  printf("oh no, we can't recover from this :(\n")
}
```

## User-Mode Cooperative Multitasking (via Coroutines)
Many languages have coroutines (a.k.a. green threads, fibers, lightweight threads, etc.). I'm sure that this isn't a novel idea, but the
basic premise is this: when we call into a coroutine, we essentially hand over control to a scheduler. Because of this, we could be
running other coroutines when it suspends. We'd essentially have lightweight cooperative multitasking.

Why not preemptive? Well, for one, preemptive multitasking is much more complex and difficult to implement correctly and it has big
performance and memory usage impacts. Plus, if you think about it, this form of cooperative multitasking could become a form of
preemptive multitasking, if you assume Haskell GHC's standards:

> GHC implements pre-emptive multitasking: the execution of threads are interleaved in a random fashion. More specifically, a thread
> may be pre-empted whenever it allocates some memory, which unfortunately means that tight loops which do no allocation tend to lock
> out other threads (this only seems to happen with pathalogical benchmark-style code, however).

Except that in our case, asynchronous I/O is what would enable us to suspend between seamlessly between different Alta coroutines,
much like Node.js's event loop.

We could also possibly adopt a Go-like thread-pool model: certain async functions marked as thread-safe can be offloaded onto another
thread. In addition, these threads would grab tasks from the scheduler to run. We could also make *all* async functions able to loaded
into another thread. If so, it would mean that *all* async functions would have to be coded to be thread-safe and to assume that they
don't know where they will be executed. This would allow for maximum usage of all available CPU resources, and would effectively be a
relaxed form of Go's Goroutines. I say "relaxed" because Go strictly doesn't allow Goroutines to share memory and has a robust
communication system. If we were to go the thread-pool route in Go's footsteps, we would not go that far. It's up to the programmer
to ensure that their code plays nice with threads.

Regardless of whether we go the thread-pool route, manually multi-threaded route, or single-threaded route, blocking I/O (or general
blocking tasks) would be offloaded onto another thread.

As an example, consider the following code:

```alta
import printLine, File from "io"
import suspend, completeAll from "coroutines"

async function getSomeNumber(input: int): int {
  # note that no suspension points are present in this coroutine
  # however, the scheduler will still continue executing
  # any other coroutines in the queue until it arrives back at
  # the original coroutine that called this corotuine

  printLine("`getSomeNumber` has been called")

  return 4 + input
}

async function inAndOut(): void {
  # normally, coroutines must be written as if they execute sequentially,
  # however, in certain cases, the author of the coroutine also controls
  # all the points at which it is called. in that case, they can assume
  # the execution type of the coroutine (e.g. synchronous, asynchronous,
  # or queued)

  printLine("this will be printed before `getSomeNumber` is called")

  await suspend()

  # in this case, we know that the only execution of this coroutine is queued
  # and we can assume, for example, that another coroutine will definitely execute
  # before we resume from our first suspension
  printLine("this will be printed after `getSomeNumber` is called")

  # this is an infinite loop
  # however, it will not block the thread, ever,
  # due to the fact that it calls `suspend`
  # this hands back control to the scheduler,
  # and since it's called on every iteration,
  # control will always be given back
  #
  # this is actually how the asynchronous I/O methods are implemented:
  # after the request for the operation is put in, the I/O method enters
  # an infinite asynchronous loop. in this loop, it checks if the operation
  # is complete. if it is, it breaks out of the loop and returns the result
  # of the operation. if it isn't, it suspends (via `await suspend()`) and
  # continues the loop on its next execution

  let index = 0
  while true {
    printLine("`inAndOut` loop, iteration #", index)
    await suspend()
  }
}

async function myAsyncCode(): int {
  printLine("starting my coroutine")

  let file = new File("some-text-file.txt")

  # this schedules `inAndOut` onto the queue to be run later;
  # it does not block neither the coroutine nor the thread
  # it is guaranteed that this will run immediately after the next suspension
  # or, if no more suspension points occur in this coroutine, before
  # returning control back outside the scheduler
  #
  # note that there is no way to retieve the coroutine's return value when
  # scheduled this way
  @Coroutines.queue inAndOut()

  # in this case, we know that `inAndOut` will run at least once before
  # `getSomeNumber` returns
  let result = await getSomeNumber(4)

  # this will asynchronously write "look at our number: 8" to the file
  # which means that `inAndOut` can execute while `write` waits for the operation
  # to complete
  await file.write("look at our number: ", result)

  # finally, exit, returning our number. because why not?
  return result
}

literal function main(argc: int, argv: ptr ptr byte): int {
  # call our coroutine synchronously
  # this essentially hands over control to the scheduler
  printLine("async executing synchronously, result = ", myAsyncCode())

  # the queued coroutine may not be done, but the original coroutine is
  # and so, `myAsyncCode` will return

  printLine("this will only print after the coroutine is done")

  # if you wanted to ensure that *all* coroutines had a chance to finish before
  # exiting, you could write the following code:
  #     completeAll()
  # this calls the `completeAll` function from the `coroutines` package, which returns
  # once the entire coroutine queue is empty
  # this essentially functions the same as if you were to join all the other threads
  # in a multi-threaded application into the current one

  return 0
}
```
