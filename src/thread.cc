/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2015 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */
#include <platform/thread.h>

Couchbase::Thread::Thread(const std::string& name_)
    : name(name_),
      state(ThreadState::Stopped) {

}

Couchbase::Thread::~Thread() {
    switch (state.load()) {
    case ThreadState::Stopped:
        return;
    case ThreadState::Zombie:
        cb_join_thread(thread_id);
        return;
    case ThreadState::Running:
        throw std::logic_error("Thread should be stopped before deleted"
                                   " (running)");
    case ThreadState::Starting:
        throw std::logic_error("Thread should be stopped before deleted"
                                   " (starting)");
    }
    throw std::logic_error("Invalid state for the Thread object");
}

void Couchbase::Thread::thread_entry() {
    cb_set_thread_name(name.c_str());

    // Call the subclass run() method
    run();


    if (state != ThreadState::Running) {
        throw std::logic_error("Couchbase::Thread::thread_entry Subclass should"
                                   " call setRunning()");
    }

    setState(ThreadState::Zombie);
}

class Couchbase::StartThreadDelegator {
public:
    static void run(Thread& thread) {
        thread.thread_entry();
    }
};

static void task_thread_main(void* arg) {
    auto* thread = reinterpret_cast<Couchbase::Thread*>(arg);
    Couchbase::StartThreadDelegator::run(*thread);
}

void Couchbase::Thread::start() {
    std::unique_lock<std::mutex> lock(synchronization.mutex);
    state = ThreadState::Starting;

    if (cb_create_thread(&thread_id, task_thread_main, this, 0) != 0) {
        state = ThreadState::Stopped;
        throw std::bad_alloc();
    }

    while (state != ThreadState::Running && state != ThreadState::Zombie) {
        synchronization.cond.wait(lock);
    }
}

void Couchbase::Thread::setRunning() {
    setState(ThreadState::Running);
}

const Couchbase::ThreadState Couchbase::Thread::waitForState(
    const Couchbase::ThreadState& newState) {

    std::unique_lock<std::mutex> lock(synchronization.mutex);

    while (true) {
        ThreadState current = getState();

        if (newState == current || current == ThreadState::Stopped) {
            // We're in the current state, or the thread is at the final state
            return current;
        }

        if (static_cast<int>(newState) < static_cast<int>(current)) {
            // We can never reach this state.
            return current;
        }
        synchronization.cond.wait(lock);
    }
}
