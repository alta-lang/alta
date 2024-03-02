use std::{iter, mem::MaybeUninit, sync::{atomic::{AtomicUsize, Ordering}, Mutex}};

use crossbeam_deque::{Injector, Worker};

pub fn execute<T, R, F, const N: usize>(tasks: [T; N], executor_fn: F) -> Vec<R>
where
	T: Send,
	R: Send,
	F: Sync + Fn(T, &dyn Fn(T)) -> R,
{
	if tasks.is_empty() {
		return vec![];
	}

	let results = Mutex::new(Vec::new());
	let injector = Injector::new();
	let worker_count = std::thread::available_parallelism().map(|x| x.get()).unwrap_or(1);
	let outstanding_work = AtomicUsize::new(tasks.len());

	let mut first_task = MaybeUninit::uninit();
	let mut is_first = true;
	for task in tasks {
		if is_first {
			is_first = false;
			first_task.write(task);
		} else {
			injector.push(task);
		}
	}

	// since we've already checked that the array isn't empty, we're guaranteed to have a first task
	let first_task = unsafe { first_task.assume_init() };

	let this_worker = Worker::<T>::new_fifo();
	this_worker.push(first_task);

	let other_workers: Vec<_> = (1..worker_count).map(|_| Worker::new_fifo()).collect();
	let stealers: Vec<_> = iter::once(&this_worker).chain(other_workers.iter()).map(|worker| worker.stealer()).collect();

	let worker_fn = |worker: Worker<T>| {
		loop {
			// try to take work from the local worker's queue;
			// otherwise, try to take it from the global queue;
			// and finally, try to take it from other worker queues.
			let maybe_work = worker.pop().or_else(|| {
				iter::repeat_with(|| {
					injector.steal_batch_and_pop(&worker).or_else(|| {
						stealers.iter().map(|stealer| stealer.steal()).collect()
					})
				})
				.find(|result| !result.is_retry())
				.and_then(|result| result.success())
			});

			match maybe_work {
				Some(work) => {
					let result = executor_fn(work, &|new_work| {
						outstanding_work.fetch_add(1, Ordering::Relaxed);
						injector.push(new_work);
					});

					outstanding_work.fetch_sub(1, Ordering::Relaxed);

					{
						let mut guard = results.lock().unwrap();
						guard.push(result);
					}
				}
				None => {
					if outstanding_work.load(Ordering::Relaxed) == 0 {
						// we're done in all workers
						//
						// we know this is true because `outstanding_work` is incremented whenever work is pushed
						// to the queue and decremented whenever work is *completed*. thus, if this is zero, all workers
						// have completed all available work and pushed no additional work.
						return;
					}
				}
			}
		}
	};

	std::thread::scope(|thread_scope| {
		let handles: Vec<_> = other_workers.into_iter().map(|worker| {
			thread_scope.spawn(move || {
				worker_fn(worker);
			})
		}).collect();

		worker_fn(this_worker);

		for handle in handles {
			handle.join().unwrap()
		}
	});

	results.into_inner().unwrap()
}
