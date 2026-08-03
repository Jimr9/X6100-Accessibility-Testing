#pragma once

enum data_type {
    DTYPE_INVALID = 0,
    DTYPE_INT,
    DTYPE_UINT64,
    DTYPE_FLOAT,
    DTYPE_GROUP,
    DTYPE_STR,
};

#ifdef __cplusplus

#include <mutex>
#include <algorithm>
#include <list>
#include <type_traits>
#include <thread>
#include <atomic>
#include <vector>

class Subject;

typedef void (*observer_cb)(Subject *, void *);

class Observer {
  protected:
    Subject *subj;
    void (*fn)(Subject *, void *);
    void *user_data;

  public:
    Observer(Subject *subj, observer_cb fn, void *user_data)
        : subj(subj), fn(fn), user_data(user_data) {};
    virtual ~Observer();
    virtual void notify();
};

class ObserverDelayed : public Observer {
    static std::list<ObserverDelayed*> delayed_observers_instances;
    static std::mutex delayed_observers_mutex;
    std::thread::id                     tid;
    std::atomic<bool>                   changed = false;

  public:
    ObserverDelayed(Subject *subj, observer_cb fn, void *user_data);
    ~ObserverDelayed();
    void        notify();
    static void notify_all_delayed();
};

class Subject {
    // Mutex to protect editing observers list
    std::mutex mutex_subscribe;


  protected:
    std::list<Observer*> observers;
    data_type type;
    // For grouped notify
    std::atomic<bool> pause_notify = false;
    std::atomic<bool> changed = false;

    std::atomic<bool> is_notifying = false;
    // Thread currently running notify() below, valid only while is_notifying
    // is true. Lets set() tell a genuine re-entrant call (same subject, same
    // thread, called from one of its own observers) apart from a call from
    // another thread, which should still block on mutex_subscribe as usual.
    std::thread::id notifying_tid;
    void notify();

    // Hook for SubjectT<T>/SubjectT<const char*>: apply a value that set()
    // deferred because it arrived re-entrantly while notify()'s pass below
    // was still running for this subject. Returns true if a value was
    // applied, so notify() runs one more full pass with the settled value.
    virtual bool apply_deferred() { return false; }

    // Subject is not designed to be deleted
    ~Subject() = default;

  public:
    virtual data_type dtype();
    Observer* subscribe(observer_cb fn, void *user_data=nullptr);
    ObserverDelayed* subscribe_delayed(observer_cb fn, void *user_data=nullptr);
    void unsubscribe(Observer *o);
    void set_pause_notify(bool val);
    void force_paused_notify();
};

template <typename T> class SubjectT : public Subject {
    static_assert(std::is_same_v<T, int32_t> || \
                  std::is_same_v<T, uint64_t> || \
                  std::is_same_v<T, float>,
                  "Unsupported type");
    std::atomic<T> val;
    std::atomic<T> pending_val{};
    std::atomic<bool> has_pending = false;

  public:
    SubjectT(T val) : val(val) {};

    T get() {
        return val;
    };

    void set(T val) {
        if (is_notifying && notifying_tid == std::this_thread::get_id()) {
            // Re-entrant: one of this subject's own observers is setting it
            // again while the notify() pass below is still running on this
            // same thread. Defer instead of writing val now - writing here
            // would let observers later in the current pass see this new
            // value early while ones already called this pass only ever saw
            // the old one, so the change would end up delivered unevenly
            // (some observers not at all until a second pass, some only
            // once anyway) instead of exactly once to every observer.
            // apply_deferred() picks this up as one clean extra pass once
            // the current one finishes.
            //
            // Always record the latest requested value unconditionally here,
            // rather than only when it differs from val (val is deliberately
            // left unwritten until apply_deferred() runs, so it's stale for
            // the rest of this pass and not a valid thing to compare a
            // second, later re-entrant write against - that comparison used
            // to let a later write that reverted back to the original value
            // get silently dropped, leaving a now-superseded earlier pending
            // value in place instead of correctly cancelling out to no
            // change at all). apply_deferred() does the one real comparison,
            // against the true pre-pass val, once the cascade has settled.
            pending_val = val;
            has_pending = true;
            return;
        }
        if (this->val != val) {
            this->val = val;
            if (this->pause_notify) {
                this->changed = true;
            } else {
                this->notify();
            }
        }
    };

    data_type dtype() {
        if (std::is_same_v<T, int32_t>)
            return DTYPE_INT;
        if (std::is_same_v<T, uint64_t>)
            return DTYPE_UINT64;
        if (std::is_same_v<T, float>)
            return DTYPE_FLOAT;
        return DTYPE_INVALID;
    }

  protected:
    bool apply_deferred() override {
        if (!has_pending) {
            return false;
        }
        has_pending = false;
        T v = pending_val;
        if (this->val != v) {
            this->val = v;
            return true;
        }
        return false;
    }
};

template <> class SubjectT<const char*> : public Subject {
    std::mutex mutex;
    std::string val;
    std::string pending_val;
    std::atomic<bool> has_pending = false;

  public:
    SubjectT<const char*>(const char* data) : val(data) {};
    char* get();
    void set(const char* data);
    data_type dtype() {
        return DTYPE_STR;
    }

  protected:
    bool apply_deferred() override;
};

/**
 * Wrapper for locking (pausing) notifications of the subjects until block end
 */
class SubjectsUpdateLock {
  private:
    std::vector<Subject *> subjects;
  public:
    SubjectsUpdateLock(std::initializer_list<Subject *> args);
    ~SubjectsUpdateLock();
};

using SubjectInt = SubjectT<int32_t>;
using SubjectUint64 = SubjectT<uint64_t>;
using SubjectFloat = SubjectT<float>;
using SubjectText = SubjectT<const char *>;

#else


typedef struct Subject Subject;
typedef struct Observer Observer;
typedef struct ObserverDelayed ObserverDelayed;

typedef void (*observer_cb)(Subject *, void *);

#endif


#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>


Subject *subject_create_int(int32_t val);
Subject *subject_create_uint64(uint64_t val);
Subject *subject_create_float(float val);
Subject *subject_create_text(const char* val);

int32_t  subject_get_int(Subject *subj);
uint64_t subject_get_uint64(Subject *subj);
float    subject_get_float(Subject *subj);
char    *subject_get_text(Subject *subj);

/// @brief Add observer to subject
/// @param subj subject to add observer
/// @param fn observer callback
/// @param user_data pointer to additional user data
/// @return observer to remove it from subject
Observer *subject_add_observer(Subject *subj, observer_cb fn, void *user_data);
Observer *subject_add_observer_and_call(Subject *subj, observer_cb fn, void *user_data);

ObserverDelayed *subject_add_delayed_observer(Subject *subj, observer_cb fn, void *user_data);
ObserverDelayed *subject_add_delayed_observer_and_call(Subject *subj, observer_cb fn, void *user_data);

enum data_type subject_get_dtype(Subject *subj);

void subject_set_int(Subject *subj, int32_t val);
void subject_set_uint64(Subject *subj, uint64_t val);
void subject_set_float(Subject *subj, float val);
void subject_set_text(Subject *subj, const char *val);

void observer_del(Observer *observer);
void observer_delayed_del(ObserverDelayed *observer);

void observer_delayed_notify_all(void);

#ifdef __cplusplus
}
#endif
