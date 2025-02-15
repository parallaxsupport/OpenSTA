// OpenSTA, Static Timing Analyzer
// Copyright (c) 2025, Parallax Software, Inc.
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
// 
// The origin of this software must not be misrepresented; you must not
// claim that you wrote the original software.
// 
// Altered source versions must be plainly marked as such, and must not be
// misrepresented as being the original software.
// 
// This notice may not be removed or altered from any source distribution.

#pragma once

#include <unordered_set>
#include "Map.hh"
#include "Set.hh"
#include "Vector.hh"
#include "LibertyClass.hh"
#include "NetworkClass.hh"
#include "MinMaxValues.hh"
#include "PinPair.hh"

namespace sta {

class Sdc;
class Clock;
class ClockEdge;
class CycleAccting;
class InputDelay;
class OutputDelay;
class FalsePath;
class PathDelay;
class MultiCyclePath;
class FilterPath;
class GroupPath;
class ExceptionFromTo;
class ExceptionFrom;
class ExceptionThru;
class ExceptionTo;
class ExceptionPt;
class InputDrive;
class MinMax;
class MinMaxAll;
class RiseFallMinMax;
class DisabledInstancePorts;
class DisabledCellPorts;
class ExceptionPath;
class DataCheck;
class Wireload;
class ClockLatency;
class ClockInsertion;
class ClockGroups;
class PortDelay;

enum class AnalysisType { single, bc_wc, ocv };

enum class ExceptionPathType { false_path, loop, multi_cycle, path_delay,
			       group_path, filter, any};

enum class ClockSense { positive, negative, stop };

typedef std::pair<const Clock*, const Clock*> ClockPair;

class ClockIndexLess
{
public:
  bool operator()(const Clock *clk1,
                  const Clock *clk2) const;
};

typedef Vector<float> FloatSeq;
typedef Vector<int> IntSeq;
typedef Vector<Clock*> ClockSeq;
typedef std::vector<const Clock*> ConstClockSeq;
typedef Set<Clock*, ClockIndexLess> ClockSet;
typedef std::set<const Clock*, ClockIndexLess> ConstClockSet;
typedef ClockSet ClockGroup;
typedef Vector<PinSet*> PinSetSeq;
typedef MinMax SetupHold;
typedef MinMaxAll SetupHoldAll;
typedef Vector<ExceptionThru*> ExceptionThruSeq;
typedef Set<LibertyPortPair, LibertyPortPairLess> LibertyPortPairSet;
typedef Map<const Instance*, DisabledInstancePorts*> DisabledInstancePortsMap;
typedef Map<LibertyCell*, DisabledCellPorts*> DisabledCellPortsMap;
typedef MinMaxValues<float> ClockUncertainties;
typedef Set<ExceptionPath*> ExceptionPathSet;
typedef PinPair EdgePins;
typedef PinPairSet EdgePinsSet;
typedef Map<const Pin*, LogicValue> LogicValueMap;

class ClockSetLess
{
public:
  bool operator()(const ClockSet *set1,
                  const ClockSet *set2) const;
};

typedef Set<ClockGroup*, ClockSetLess> ClockGroupSet;

// For Search.
class ExceptionState;

class ExceptionStateLess
{
public:
  bool operator()(const ExceptionState *state1,
                  const ExceptionState *state2) const;
};

class ExceptionPath;
class ExceptionStates {
public:
   typedef Set<ExceptionState*, ExceptionStateLess> Super;
   ExceptionStates() : holder_(nullptr), hasLoopPath_(false), hasFilterPath_(false) {}
   ExceptionStates(ExceptionStates&& o)  noexcept : holder_(o.holder_), hasLoopPath_(o.hasLoopPath_), hasFilterPath_(o.hasFilterPath_) {
    o.holder_ = nullptr;
   }
   ExceptionStates(const ExceptionStates& o) = delete;
   ExceptionStates& operator=(const ExceptionStates & o) = delete;
   ~ExceptionStates() { delete holder_; }
   explicit operator bool () const { return holder_; }
   bool hasLoopPath() const { return hasLoopPath_; }
   bool hasFilterPath() const { return hasFilterPath_; }

   void insert(ExceptionState* state);
   void clear();

   void to(Super& s) { if (holder_) s.swap(*holder_); }
   void takeOver(ExceptionStates&& o) {
    clear();
    holder_ = o.holder_;
    hasLoopPath_ = o.hasLoopPath_;
    hasFilterPath_ = o.hasFilterPath_;
    o.holder_ = nullptr;
   }
private:
   Super         *holder_;
   bool           hasLoopPath_;
   bool           hasFilterPath_;
};

class ExceptionStateSet {
  struct Impl : public Set<ExceptionState*, ExceptionStateLess> {
	typedef Set<ExceptionState*, ExceptionStateLess> Super;
	Impl(ExceptionStates & tmp) : Super(), hash_(hash_init_value), refcount(0), hasLoopPath_(tmp.hasLoopPath()), hasFilterPath_(tmp.hasFilterPath()) { tmp.to(super()); rehash(); }
	size_t hash() const { return hash_; }
    bool operator == (Impl const & i) const;

    size_t   hash_;
    unsigned refcount      :30;
	bool     hasLoopPath_  :1;
	bool     hasFilterPath_:1;
  protected:
	Super& super() { return *this; }
	void rehash();
  };
  Impl* impl;
public:
  ExceptionStateSet(ExceptionStates & tmp) : impl(create(tmp)) { incr(impl); }
  ExceptionStateSet(const ExceptionStateSet & o) : impl(o.impl) { incr(impl); }
  ExceptionStateSet& operator=(const ExceptionStateSet & o) {
    Impl* i = o.impl; incr(i); decr(impl); impl = i; return *this;
  }
  explicit operator bool () const { return impl; }
  bool operator==(const ExceptionStateSet& o) const;
  bool empty() const { return !impl; }
  size_t size() const { return impl ? impl->size() : 0; }
  struct ConstIterator : public Impl::Super::ConstIterator {
    ConstIterator(const ExceptionStateSet& set) : Impl::Super::ConstIterator(set.impl) {}
  };
  Impl::Super::const_iterator begin() const { return impl->begin(); }
  Impl::Super::const_iterator end() const { return impl->end(); }
  int cmp(ExceptionStateSet const & o) const;
  bool hasLoopPath()   const { return impl && impl->hasLoopPath_; }
  bool hasFilterPath() const { return impl && impl->hasFilterPath_; }
  size_t hash() const { return impl ? impl->hash() : 0; }
private:
  void incr(Impl* i) { if (i) ++i->refcount; }
  void decr(Impl* i) { if (i && --i->refcount == 0) { mgr.erase(i); delete i; } }
  Impl* create(ExceptionStates & tmp);

  struct ImplHash { size_t operator()(Impl* i) const { return i ? i->hash() : 0; } };
  struct ImplEqual { bool operator()(Impl* a, Impl* b) const { return a == b || (a && b && *a == *b); } };
  typedef std::unordered_set<Impl*,ImplHash, ImplEqual> Manager;
  static Impl* unique(Impl* i);
  static thread_local Manager mgr;
};

enum class CrprMode { same_pin, same_transition };

// Constraint applies to clock or data paths.
enum class PathClkOrData { clk, data };

const int path_clk_or_data_count = 2;

enum class TimingDerateType { cell_delay, cell_check, net_delay };
constexpr int timing_derate_type_count = 3;
enum class TimingDerateCellType { cell_delay, cell_check };
constexpr int timing_derate_cell_type_count = 2;

} // namespace
