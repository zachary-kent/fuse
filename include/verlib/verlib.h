// This selects between using versioned objects or regular objects
// Versioned objects are implemented as described in:
//   Wei, Ben-David, Blelloch, Fatourou, Rupert and Sun,
//   Constant-Time Snapshots with Applications to Concurrent Data Structures
//   PPoPP 2021
// They support snapshotting via version chains, and without
// indirection, but pointers to objects (ptr_type) must be "recorded
// once" as described in the paper.
#ifndef VERLIB_LIBRARY_H_
#define VERLIB_LIBRARY_H_

#include <parlay/parallel.h>
#include <parlay/sequence.h>
#include "flock/flock.h"

#if defined(TINYSTM) || defined(TL2O)
#define CRAPYSTM 1
#endif

// Default stm::New / stm::Delete for builds without a glue (the upstream
// flock benchmark binaries, _flock targets). Structures call stm::New /
// stm::Delete directly; when OL_USE_STM is set, the user's glue header is
// expected to have provided its own stm:: namespace before this header
// is included, in which case these fallbacks are shadowed.
#ifndef OL_USE_STM
namespace stm {
  template <typename T, typename... Args>
  inline T* New(Args&&... args) {
    return epoch::memory_pool_<T>::New(std::forward<Args>(args)...);
  }
  template <typename T>
  inline void Delete(T* p) {
    epoch::memory_pool_<T>::Retire(p);
  }
}
#endif

#if defined(Versioned)
#define WeakLoad
#define AtomicSingleton
  
// versioned objects, ptr_type includes version chains
#if defined(Recorded_Once)
#include "versioned_recorded_once.h"
#elif defined(FullyIndirect)
#include "versioned_indirect.h"
#elif defined(GenSnapshot)
#include "versioned_generalized.h"
#else
#include "versioned_hybrid.h"
#endif // Recorded_Once

namespace verlib {
  template <typename F>
  auto atomic_region(const F& f) {
    return epoch::with_epoch([&] { 
      if constexpr (std::is_void_v<std::invoke_result_t<F>>) {f();}
      else {return *(f());}
    });
  }

  template <typename F>
  auto atomic_read_only(const F& f) {
    return epoch::with_epoch([&] {
      return f();});
  }
}

#elif defined(MV_TLF_STM)
#include "../fuse/transactions_nwl.h"
#define WeakLoad
#define MV_TR

#elif defined(MV_TLF)
#include "../fuse/transactions.h"
#define WeakLoad
#define AtomicSingleton
#define MV_TR

#elif defined(MV_STM)
#include "../fuse/stm.h"
#define MV_TR

#elif defined(SV_TLF_STM)
#define WeakLoad
#include "../other_stms/tlf_glue.h"

#elif defined(SV_TLF)
#define WeakLoad
#include "../other_stms/tlf_glue.h"

#elif defined(SV_STM)
#include "../other_stms/stm_glue.h"
#else // Not Versioned or Transactional
#define WeakLoad
#define AtomicSingleton

namespace verlib {

#ifdef OL_USE_STM
  // Route the structures' versioned_ptr / atomic_bool / lock through
  // stm:: so that when stm:: is wired to a transactional backend (e.g.
  // uSTM with TLF support, or fuse::tlf_atomic) the structures' loads,
  // stores, and lock acquisitions participate in the backend's tracking.
  // The caller must define stm:: before including this header (e.g. by
  // including a glue header that provides stm::atomic, stm::versioned,
  // and stm::lock). When stm::atomic is just a typedef alias for
  // flck::atomic and stm::lock for flck::lock (the OL-only build), this
  // is zero overhead.
  using versioned = ::stm::versioned;
  template <typename T>
  using versioned_ptr = ::stm::atomic<T*>;
  using atomic_bool = ::stm::atomic<bool>;
  using lock = ::stm::lock;
#else
  struct versioned {};
  template <typename T>
  using versioned_ptr = flck::atomic<T*>;
  using atomic_bool = flck::atomic<bool>;
  using flck::lock;
#endif
  template <typename A, typename B, typename C>
  bool validate(const A& a, const B& b, const C& c) {return true;}

  template <typename F>
  auto with_snapshot(F f, bool unused_parameter=false) {
#ifdef OL_USE_STM
    return ::stm::with_epoch([&] { return f();});
#else
    return flck::with_epoch([&] { return f();});
#endif
  }

  template <typename F>
  auto atomic_read_only(F f) {
#ifdef OL_USE_STM
    return ::stm::with_epoch([&] { return f();});
#else
    return flck::with_epoch([&] { return f();});
#endif
  }

  template <typename F>
  auto atomic_region(const F& f) {
    return epoch::with_epoch([&] {
      if constexpr (std::is_void_v<std::invoke_result_t<F>>) {f();}
      else {return *(f());}
    });
  }

}
#endif // Versioned

namespace verlib {
#ifdef OL_USE_STM
  // Route epoch protection through the configured stm:: backend instead
  // of flck. This is required when the structures' memory_pool is also
  // routed through stm:: (e.g., uSTM's uepoch-based pool), so retired
  // nodes are protected by the same EBR that the STM uses for its own
  // read/write log.
  template <typename F>
  auto with_epoch(F&& f) {
    return ::stm::with_epoch(std::forward<F>(f));
  }
#else
  using flck::with_epoch;
#endif

#if defined(WeakLoad)
#define AtomicSingletonReadOnly
  template <typename F>
  bool validate(const F& f) { return f();}
#else
  // if there are no weak loads then no validation is needed
  template <typename F>
  bool validate(const F& f) {return true;}
#endif
}

#endif // VERLIB_LIBRARY_H_
