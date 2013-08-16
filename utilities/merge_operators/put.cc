#include <memory>
#include "leveldb/slice.h"
#include "leveldb/merge_operator.h"
#include "utilities/merge_operators.h"

using namespace leveldb;

namespace { // anonymous namespace

// A merge operator that mimics Put semantics
// Since this merge-operator will not be used in production,
// it is implemented as a non-associative merge operator to illustrate the
// new interface and for testing purposes. (That is, we inherit from
// the MergeOperator class rather than the AssociativeMergeOperator
// which would be simpler in this case).
//
// From the client-perspective, semantics are the same.
class PutOperator : public MergeOperator {
 public:
  virtual bool Merge(const Slice& key,
                     const Slice* existing_value,
                     const std::deque<std::string>& operand_sequence,
                     std::string* new_value,
                     Logger* logger) const override {
    // Put basically only looks at the current/latest value
    assert(!operand_sequence.empty());
    assert(new_value != nullptr);
    new_value->assign(operand_sequence.back());
    return true;
  }

  virtual bool PartialMerge(const Slice& key,
                            const Slice& left_operand,
                            const Slice& right_operand,
                            std::string* new_value,
                            Logger* logger) const override {
    new_value->assign(right_operand.data(), right_operand.size());
    return true;
  }

  virtual const char* Name() const override {
    return "PutOperator";
  }
};

} // end of anonymous namespace

namespace leveldb {

std::shared_ptr<MergeOperator> MergeOperators::CreatePutOperator() {
  return std::make_shared<PutOperator>();
}

}