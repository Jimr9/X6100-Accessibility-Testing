#include "subjects.h"

extern "C" {
    #include "../lvgl/lvgl.h"
    #include <stdint.h>
    #include <stdio.h>
    #include <stdlib.h>
}

std::list<ObserverDelayed*> ObserverDelayed::delayed_observers_instances;
std::mutex ObserverDelayed::delayed_observers_mutex;

Observer::~Observer() {
    subj->unsubscribe(this);
}

void Observer::notify() {
    this->fn(subj, user_data);
};

ObserverDelayed::ObserverDelayed(Subject *subj, observer_cb fn, void *user_data) : Observer(subj, fn, user_data) {
    const std::lock_guard<std::mutex> lock(delayed_observers_mutex);
    tid = std::this_thread::get_id();
    delayed_observers_instances.push_back(this);
};

ObserverDelayed::~ObserverDelayed() {
    const std::lock_guard<std::mutex> lock(delayed_observers_mutex);
    auto item = std::find(delayed_observers_instances.begin(), delayed_observers_instances.end(), this);
    delayed_observers_instances.erase(item);
}

void ObserverDelayed::notify() {
    auto call_tid = std::this_thread::get_id();
    if (call_tid != tid) {
        changed = true;
    } else {
        this->Observer::notify();
        changed = false;
    }
}

void ObserverDelayed::notify_all_delayed() {
    const std::lock_guard<std::mutex> lock(delayed_observers_mutex);
    for (auto& item: ObserverDelayed::delayed_observers_instances) {
        if (item->changed) {
            item->Observer::notify();
            item->changed = false;
        }
    }
}

Observer* Subject::subscribe(observer_cb fn, void *user_data) {
    const std::lock_guard<std::mutex> lock(mutex_subscribe);

    auto observer = new Observer(this, fn, user_data);
    observers.push_back(observer);
    return observer;
}

ObserverDelayed* Subject::subscribe_delayed(observer_cb fn, void *user_data) {
    const std::lock_guard<std::mutex> lock(mutex_subscribe);

    auto observer = new ObserverDelayed(this, fn, user_data);
    observers.push_back(observer);
    return observer;
}

void Subject::unsubscribe(Observer *observer) {
    const std::lock_guard<std::mutex> lock(mutex_subscribe);
    observers.erase(std::find(observers.begin(), observers.end(), observer));
}

void Subject::set_pause_notify(bool val) {
    this->pause_notify = val;
}

void Subject::force_paused_notify() {
    if (this->pause_notify && this->changed) {
        this->notify();
    }
    this->changed = false;
    this->pause_notify = false;
}

void Subject::notify() {
    // Local-only fix for upstream issue #261 (band-switch/tuning
    // self-deadlock). A re-entrant call happens when an observer of this
    // subject sets it again, on the same thread, while the pass below is
    // still running - an earlier stopgap version of this fix just made that
    // re-entrant call a no-op, which avoided the deadlock but let the value
    // change leak into the in-progress pass mid-flight: observers not yet
    // called in that pass would see the new value early, while observers
    // already called only ever saw the old one, so the change ended up
    // delivered unevenly instead of once to every observer.
    //
    // set() (SubjectT<T>/SubjectT<const char*>) now defers the actual value
    // change via apply_deferred() instead of writing it here and recursing,
    // so the in-progress pass below finishes against one consistent value,
    // and the deferred value is picked up as exactly one more full pass -
    // see the comment at set() for the same reasoning.
    //
    // gdyuldin is working on an upstream fix for #261 - once that lands,
    // this should be reconciled/removed rather than kept alongside it.
    if (is_notifying && notifying_tid == std::this_thread::get_id()) {
        return;
    }
    is_notifying  = true;
    notifying_tid = std::this_thread::get_id();
    do {
        const std::lock_guard<std::mutex> lock(mutex_subscribe);
        for (auto& observer : observers) {
            observer->notify();
        }
    } while (apply_deferred());
    is_notifying = false;
}

data_type Subject::dtype() {
    return DTYPE_INVALID;
}

/**
 * String subject
 */


char *SubjectT<const char *>::get() {
    const std::lock_guard<std::mutex> lock(mutex);
    return strdup(val.data());
}

void SubjectT<const char *>::set(const char *data) {
    if (is_notifying && notifying_tid == std::this_thread::get_id()) {
        // See the matching comment in SubjectT<T>::set() (subjects.h) and in
        // Subject::notify() - deferred so the in-progress pass sees one
        // consistent value throughout instead of a mix of old and new.
        // Always record unconditionally (not gated on this->val, which is
        // stale until apply_deferred() runs) so a later re-entrant write in
        // the same pass that reverts back to the original value is judged
        // against the true pre-pass val in apply_deferred(), not against a
        // now-superseded earlier pending value.
        const std::lock_guard<std::mutex> lock(mutex);
        pending_val = data;
        has_pending = true;
        return;
    }

    std::string new_val(data);
    bool        changed = false;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        if (this->val != new_val) {
            changed   = true;
            this->val = new_val;
        }
    }
    if (changed) {
        if (this->pause_notify) {
            this->changed = true;
        } else {
            this->notify();
        }
    }
}

bool SubjectT<const char *>::apply_deferred() {
    const std::lock_guard<std::mutex> lock(mutex);
    if (!has_pending) {
        return false;
    }
    has_pending = false;
    if (this->val != pending_val) {
        this->val = pending_val;
        return true;
    }
    return false;
}

SubjectsUpdateLock::SubjectsUpdateLock(std::initializer_list<Subject *> args) {
    subjects = args;
    for (const auto& s: subjects) {
        s->set_pause_notify(true);
    }

}

SubjectsUpdateLock::~SubjectsUpdateLock() {
    for (const auto& s: subjects) {
        s->force_paused_notify();
    }
}

/*
    C-API
*/


Subject *subject_create_int(int32_t val) {
    return new SubjectT(val);
}

Subject *subject_create_uint64(uint64_t val) {
    return new SubjectT(val);
}

Subject *subject_create_float(float val) {
    return new SubjectT(val);
}

Subject *subject_create_text(const char *val) {
    return new SubjectT(val);
}

int32_t subject_get_int(Subject *subj) {
    return static_cast<SubjectInt*>(subj)->get();
}

uint64_t subject_get_uint64(Subject *subj) {
    return static_cast<SubjectUint64*>(subj)->get();
}

float subject_get_float(Subject *subj) {
    return static_cast<SubjectFloat*>(subj)->get();
}

char *subject_get_text(Subject *subj) {
    return static_cast<SubjectText*>(subj)->get();
}

void subject_set_int(Subject *subj, int32_t val) {
    if (subj->dtype() == DTYPE_INT) {
        static_cast<SubjectInt*>(subj)->set(val);
    } else {
        LV_LOG_ERROR("Expected subject with dtype int, got %u\n", subj->dtype());
    }
}
void subject_set_uint64(Subject *subj, uint64_t val) {
    if (subj->dtype() == DTYPE_UINT64) {
        static_cast<SubjectUint64*>(subj)->set(val);
    } else {
        LV_LOG_ERROR("Expected subject with dtype uint64, got %u\n", subj->dtype());
    }
}

void subject_set_float(Subject *subj, float val) {
    if (subj->dtype() == DTYPE_FLOAT) {
        static_cast<SubjectFloat*>(subj)->set(val);
    } else {
        LV_LOG_ERROR("Expected subject with dtype float, got %u\n", subj->dtype());
    }
}

void subject_set_text(Subject *subj, const char *val) {
    if (subj->dtype() == DTYPE_STR) {
        static_cast<SubjectText*>(subj)->set(val);
    } else {
        LV_LOG_ERROR("Expected subject with dtype str, got %u\n", subj->dtype());
    }
}

Observer *subject_add_observer(Subject *subj, observer_cb fn, void *user_data) {
    return subj->subscribe(fn, user_data);
}

Observer *subject_add_observer_and_call(Subject *subj, observer_cb fn, void *user_data) {
    auto observer = subj->subscribe(fn, user_data);
    fn(subj, user_data);
    return observer;
}

ObserverDelayed *subject_add_delayed_observer(Subject *subj, observer_cb fn, void *user_data) {
    return subj->subscribe_delayed(fn, user_data);
}

ObserverDelayed *subject_add_delayed_observer_and_call(Subject *subj, observer_cb fn, void *user_data) {
    auto observer = subj->subscribe_delayed(fn, user_data);
    fn(subj, user_data);
    return observer;
}

data_type subject_get_dtype(Subject *subj) {
    return subj->dtype();
}

void observer_del(Observer *observer) {
    delete observer;
}
void observer_delayed_del(ObserverDelayed *observer) {
    delete observer;
}
void observer_delayed_notify_all(void) {
    ObserverDelayed::notify_all_delayed();
};
