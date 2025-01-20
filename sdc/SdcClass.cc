#include "SdcClass.hh"
#include "Sdc.hh"

namespace sta {

void
ExceptionStates::insert(ExceptionState* state) {
  if (!holder_) holder_ = new Super;
  holder_->insert(state);
  if (!hasLoopPath_ && state->exception()->isLoop())     hasLoopPath_ = true;
  if (!hasFilterPath_ && state->exception()->isFilter()) hasFilterPath_ = true;
}

void
ExceptionStates::clear() {
  delete holder_;
  holder_ = nullptr;
  hasLoopPath_ = false;
}

bool
ExceptionStateSet::Impl::operator == (Impl const & i) const {
  return (*(Super*)this) == (*(Super*)&i);
}

void
ExceptionStateSet::Impl::rehash() {
  hash_ = hash_init_value;
  for(auto* state : *this) {
    hashIncr(hash_, state->hash());
  }
}

bool
ExceptionStateSet::operator==(const ExceptionStateSet& o) const {
  return impl == o.impl;
}

thread_local ExceptionStateSet::Manager
ExceptionStateSet::mgr;

ExceptionStateSet::Impl*
ExceptionStateSet::unique(Impl* i) {
  if (i->empty()) {
    delete i;
    return nullptr;
  }
  auto r = mgr.insert(i);
  if (!r.second) {
	delete i;
	i = *r.first;
  }
  return i;
}

ExceptionStateSet::Impl*
ExceptionStateSet::create(ExceptionStates & tmp) {
  return tmp ? unique(new Impl(tmp)) : nullptr;
}

int
ExceptionStateSet::cmp (const ExceptionStateSet & o) const {
  size_t sz1 = size();
  size_t sz2 = o.size();
  if (sz1 != sz2) return sz1 < sz2 ? 1 : -1;
  if (!sz1) return true;
  auto it1 = begin();
  auto it2 = o.begin();
  while (*it1 == *it2) {
    ++it1, ++it2;
  }
  if (it1 == end()) return 0;
  return *it1 < *it2 ? 1 : -1;
}

} // end namespace sta