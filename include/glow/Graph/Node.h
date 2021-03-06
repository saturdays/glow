/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef GLOW_GRAPH_NODE_H
#define GLOW_GRAPH_NODE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"

#include "glow/Base/Traits.h"
#include "glow/Base/Type.h"
#include "glow/Graph/NodeValue.h"
#include "glow/Graph/UseDef.h"

#include <list>

namespace glow {

class Function;
class Node;
class NodeWalker;
struct NodeUse;
template <bool is_const_iter> class NodeValueIteratorImpl;
using NodeValueIterator = NodeValueIteratorImpl<false>;
using NodeValueConstIterator = NodeValueIteratorImpl<true>;

/// Represents a node in the compute graph.
class Node : public Named,
             public Kinded,
             public UseDef<Node, NodeUse>,
             public llvm::ilist_node<Node> {
  friend llvm::ilist_traits<Node>;

protected:
  /// This is the maximum number of results that a node may have.
  static constexpr unsigned maxNodeResno_ = 6;

  /// The output types for the results of the node.
  std::array<TypeRef, maxNodeResno_> types_;
  /// The number of results that the node has.
  unsigned numRes_{0};
  /// A nullable reference to some tensor value that may predicate the execution
  /// of the current node.
  NodeHandle predicate_;

  /// Destroys a node and deallocates the memory. This method is typically
  /// implicitly invoked when a node is being removed from the intrusive list of
  /// nodes.
  static void destroyNode(Node *N);

  /// Link to the function holding this node.
  Function *parent_;

public:
  Node(Kinded::Kind k, llvm::StringRef name)
      : Named(name), Kinded(k), predicate_(this, nullptr), parent_(nullptr) {}

  /// \returns the nullable predicate of the current node.
  const NodeValue getPredicate() const;
  /// Assigns a nullable predicate to the current node.
  void setPredicate(const NodeValue &P);
  /// Checks if a predicate is assigned to the current node.
  bool hasPredicate() const;

  /// \returns the number of results that the node has.
  unsigned getNumResults() const { return numRes_; }
  /// \returns the \p idx result of the node.
  NodeValue getNthResult(unsigned idx);
  /// \returns the n'th result of the node.
  const NodeValue getNthResult(unsigned idx) const;

  /// \returns the function holding this node.
  /// If that node does not belong to any function, this
  /// is nullptr.
  const Function *getParent() const { return parent_; }
  Function *getParent() { return parent_; }
  /// Set the link to the function that holds this node.
  void setParent(Function *parent) { parent_ = parent; }

  /// Getters/setters to access Node's inputs and outputs.
  unsigned getNumInputs() const;
  std::string getInputName(unsigned idx) const;
  NodeValue getNthInput(unsigned idx);
  const NodeValue getNthInput(unsigned idx) const;
  void setNthInput(unsigned idx, NodeValue val);
  llvm::StringRef getOutputName(unsigned idx) const;
  bool hasSideEffects() const;
  bool isArithmetic() const;

  /// \returns true if this input is being overwritten by the node.
  bool isOverwrittenNthInput(unsigned idx) const;

  /// \returns a textual description of the node.
  std::string getDebugDesc() const;

  /// \returns copy of the current node. Notice that the new node is not
  /// inserted into any DAG. The caller of this method should add it to some
  /// node-list.
  Node *clone() const;

  /// \returns true if the node is equal to the other node.
  bool isEqual(const Node &other) const;

  /// \returns true if the node is equal to the other node.
  bool operator==(const Node &O) const { return isEqual(O); }

  /// \returns a hash code of the node.
  llvm::hash_code getHash() const;

  /// This method implements the visitor pattern that scans the compute DAG top
  /// to bottom. The visitor \p visitor is sent by the parent node \p parent,
  /// or nullptr if this is the first node to be visited.
  void visit(Node *parent, NodeWalker *visitor);

  void visit(const Node *parent, NodeWalker *visitor) const;

  /// Verify node.
  /// \returns True if the node is valid. False otherwise.
  bool verify() const;

  /// Replace all uses of this node with null. This method is used by the
  /// destruction sequence. When the node is deleted we need to unregister all
  /// users. This allows us to deconstruct the graph in an arbitrary order.
  void releaseUsers() {
    NodeValue nop(nullptr);
    for (unsigned i = 0; i < getNumResults(); i++) {
      NodeValue(this, i).replaceAllUsesOfWith(nop);
    }
  }

  ~Node() { releaseUsers(); }

  /// \returns the n'th result type of the node.
  TypeRef getType(unsigned idx) const;
  /// Set the \p idx'th result type of the node.
  /// \note This setter only changes the type of this one
  ///       result. If that type is incompatible with
  ///       the inputs of the node, the caller is
  ///       responsible to update these if need be.
  void setType(unsigned idx, TypeRef ty);

  /// Methods that forward to the result type (that must be valid):
  /// @{
  ElemKind getElementType(unsigned resNo) const;
  llvm::ArrayRef<size_t> dims(unsigned resNo) const;
  /// @}

protected:
  /// When constructing the node, add a new result of type \p T.
  void addResult(TypeRef T);
};

/// A walker that recursively visits a node and its children.
class NodeWalker {
public:
  /// This callback is called before visiting the children of \p N.
  virtual void pre(Node *parent, Node *N) {}
  virtual void pre(const Node *parent, const Node *N) {}

  /// This callback is called after visiting the children of \p N.
  virtual void post(Node *parent, Node *N) {}
  virtual void post(const Node *parent, const Node *N) {}

  /// This callback is called before processing the graph. If the method returns
  /// false then we skip this node.
  virtual bool shouldVisit(Node *parent, Node *N) { return true; }
  virtual bool shouldVisit(const Node *parent, const Node *N) { return true; }

  /// Dtor.
  virtual ~NodeWalker() = default;
};

/// Helper class to walk through the specific uses of a NodeValue.
/// This class is built on top of the regular users-list (Node::getUsers)
/// but filters out the uses that don't affect the desired NodeValue.
template <bool is_const_iter = false>
class NodeValueIteratorImpl
    : public std::iterator<std::forward_iterator_tag, NodeUse> {
public:
  /// Base type of the iterator.
  using iterator =
      typename std::conditional<is_const_iter,
                                std::list<NodeUse>::const_iterator,
                                std::list<NodeUse>::iterator>::type;
  /// Type of the NodeValue that this iterator is filtering for.
  using NodeValueTy = typename std::conditional<is_const_iter, const NodeValue,
                                                NodeValue>::type;
  /// Type of the NodeUse that this iterator should return when dereferenced.
  using NodeUseTy =
      typename std::conditional<is_const_iter, const NodeUse, NodeUse>::type;

private:
  /// NodeValue that this iterator tracks.
  NodeValueTy &parent_;
  /// Actual iterator on the users-list.
  /// \invariant if it_ points to a valid iterator, then the NodeValue it
  /// references (via the NodeUse) is equal to parent_.
  iterator it_;

  /// Convenient method to get the end iterator of the users list that this
  /// iterator walks.
  iterator getEnd() const { return parent_.getNode()->getUsers().end(); }

  /// Check if \p it_ points to a NodeUse that references \p parents_.
  bool hasSameParent() const {
    assert(it_ != getEnd() && "Cannot check invalid iterator");
    // A users-list should be for one node.
    // If this assert breaks, that means the input list is broken,
    // or this iterator is not used as it was intended: to walk
    // through a users-list.
    assert(it_->get()->getNode() == parent_.getNode() &&
           "Iterator points to a list with different parent?!");
    return it_->get()->getResNo() == parent_.getResNo();
  }

public:
  NodeValueIteratorImpl(NodeValueTy &parent, iterator it)
      : parent_(parent), it_(it) {
    if (it_ != getEnd() && !hasSameParent()) {
      ++(*this);
    }
    assert((it_ == getEnd() || hasSameParent()) &&
           "operator++ should return the next valid iterator");
  }

  /// Move to the next use of parent_.
  NodeValueIteratorImpl &operator++() {
    auto endIt = getEnd();
    while (++it_ != endIt && !hasSameParent()) {
    }
    return *this;
  }

  NodeUseTy &operator*() {
    assert(hasSameParent() && "Invalid iterator");
    return *it_;
  }

  const NodeUseTy &operator*() const {
    assert(hasSameParent() && "Invalid iterator");
    return *it_;
  }

  bool operator!=(const NodeValueIteratorImpl &other) const {
    return it_ != other.it_;
  }
};

} // namespace glow

namespace llvm {
/// Allow casting NodeValue into Node*.
template <> struct simplify_type<glow::NodeValue> {
  typedef glow::Node *SimpleType;
  static SimpleType getSimplifiedValue(glow::NodeValue &val) {
    return val.getNode();
  }
};

/// Allow casting NodeValue into Node*.
template <> struct simplify_type<const glow::NodeValue> {
  typedef glow::Node *SimpleType;
  static SimpleType getSimplifiedValue(const glow::NodeValue &val) {
    return val.getNode();
  }
};

/// Allow casting NodeHandle into Node*.
template <> struct simplify_type<glow::NodeHandle> {
  typedef glow::Node *SimpleType;
  static SimpleType getSimplifiedValue(glow::NodeHandle &val) {
    return val.getNode();
  }
};
/// Allow casting const NodeHandle into Node*.
template <> struct simplify_type<const glow::NodeHandle> {
  typedef glow::Node *SimpleType;
  static SimpleType getSimplifiedValue(const glow::NodeHandle &val) {
    return val.getNode();
  }
};

//===----------------------------------------------------------------------===//
// ilist_traits for glow::Node
//===----------------------------------------------------------------------===//

template <>
struct ilist_traits<glow::Node> : public ilist_node_traits<glow::Node> {
  using Node = glow::Node;

  glow::Function *getContainingFunction();

private:
  using node_iterator = simple_ilist<Node>::iterator;

public:
  static void deleteNode(Node *N) { glow::Node::destroyNode(N); }

  void addNodeToList(Node *N);
  void removeNodeFromList(Node *N);
  void transferNodesFromList(ilist_traits<Node> &L2, node_iterator first,
                             node_iterator last);

private:
  void createNode(const Node &);
};

} // namespace llvm

// custom specialization of std::hash for NodeValue.
namespace std {
template <> struct hash<glow::NodeValue> {
  typedef glow::NodeValue argument_type;
  typedef std::size_t result_type;
  result_type operator()(argument_type const &s) const noexcept {
    auto name = s.getNode()->getName();
    result_type const h1(std::hash<std::string>{}(name.str()));
    result_type const h2(std::hash<unsigned>{}(s.getResNo()));
    return h1 ^ (h2 << 8);
  }
};
} // namespace std

#endif // GLOW_GRAPH_NODE_H
