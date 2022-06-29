/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <stack>

#include "OccSolver.h"

#include <folly/Format.h>

namespace facebook {
namespace terragraph {

const float OccSolver::kHighVertexWeight{10000.0};
const float OccSolver::kLowVertexWeight{1.0};
const float OccSolver::kDefaultVertexWeight{100.0};
const float OccSolver::kBiasUpVertexWeight{100.1};
const float OccSolver::kBiasDownVertexWeight{99.9};

OccSolver::OccSolver(
    const SimpleGraph& graph,
    const std::unordered_map<std::string, float>& vertexWeights)
    : graph_(graph), vertexWeights_(vertexWeights) {
  tree_ = SimpleGraph(true);  // directed graph
}

OccSolver::OccSolver(
    const TopologyWrapper& topologyW,
    const std::unordered_map<std::string, float>& siteWeights)
    : vertexWeights_(siteWeights) {

  graph_ = SimpleGraph(false);  // undirected graph
  tree_ = SimpleGraph(true);  // directed graph

  buildGraph(topologyW);
}

void
OccSolver::buildGraph(const TopologyWrapper& topologyW) {
  // Add sites as vertices
  for (const auto& site : topologyW.getAllSites()) {
    graph_.addVertex(site.name);
  }

  // Add wireless links as edges
  for (const auto& link : topologyW.getAllLinks()) {
    if (link.link_type != thrift::LinkType::WIRELESS) {
      continue;
    }
    auto aNode = topologyW.getNode(link.a_node_name);
    auto zNode = topologyW.getNode(link.z_node_name);
    if (aNode && zNode && aNode->site_name != zNode->site_name) {
      graph_.addEdge(aNode->site_name, zNode->site_name);
    }
  }
}

std::vector<std::pair<std::string, std::string>>
OccSolver::fillin(
    const SimpleGraph& g, const std::unordered_set<std::string>& vNames) const {
  std::vector<std::pair<std::string, std::string>> fill;
  // for all vertex combinations of 2
  for (auto it1 = vNames.begin(); it1 != vNames.end(); ++it1) {
    for (auto it2 = std::next(it1); it2 != vNames.end(); ++it2) {
      if (!g.isNeighbor(*it1, *it2)) {
        fill.push_back(std::make_pair(*it1, *it2));
      }
    }
  }
  return fill;
}

size_t
OccSolver::priority(const SimpleGraph& g, const std::string& vName) const {
  auto neighbors = g.getNeighbors(vName);
  return fillin(g, neighbors).size();
}

std::string
OccSolver::minPriorityVertex(const SimpleGraph& g) const {
  size_t minLen = std::numeric_limits<std::size_t>::max();
  std::string minLenVertex;
  for (const auto& vName : g.getVertices()) {
    auto p = priority(g, vName);
    if (p <= minLen) {
      minLen = p;
      minLenVertex = vName;
    }
  }
  return minLenVertex;
}

void
OccSolver::addTreeVertex(
    const std::string& vName,
    const std::unordered_set<std::string>& bag,
    int step) {
  VLOG(4) << folly::format(
      "Adding tree vertex {} with properties (step: {}, bag: {})",
      vName,
      step,
      folly::join(", ", bag));

  tree_.addVertex(vName);
  TreeVertexProperties treeVertexProp;
  treeVertexProp.step = step;
  treeVertexProp.bag = bag;
  nameToTreeVertexProp_[vName] = treeVertexProp;
}

void
OccSolver::greedyTreeDecomp() {
  // Use a copy of the graph as it gets manipulated
  auto graph = graph_;

  // Initializalize to run greedy algorithm
  int step = 0;
  std::stack<std::string> eliminationOrder;

  // While there are still vertices to be eliminated
  while (graph.numVertices() > 0) {
    // advance one step
    ++step;

    // pick vertex to eliminate
    auto elimVertex = minPriorityVertex(graph);
    eliminationOrder.push(elimVertex);
    VLOG(4) << folly::format("{}: Eliminating {}", step, elimVertex);

    // Create bag of neighbors
    auto bag = graph.getNeighbors(elimVertex);

    // Eliminate node and add fill
    auto newEdges = fillin(graph, bag);
    graph.addEdges(newEdges);
    graph.removeVertex(elimVertex);

    // Add eliminated vertex to bag
    bag.insert(elimVertex);

    // Add eliminated vertex to tree
    addTreeVertex(elimVertex, bag, step);
  }

  // last vertex to be eliminated becomes the root
  rootVertex_ = eliminationOrder.top();
  eliminationOrder.pop();

  // Set the parent of the remaining vertices in reverse order of creation
  while (!eliminationOrder.empty()) {
    auto childVertexName = eliminationOrder.top();
    eliminationOrder.pop();

    // Get vertex in node's bag with smallest step attribute
    // and make it node's parent in the tree decomposition
    auto minStep = step + 1;
    auto parentVertexName = rootVertex_;
    for (const auto& nVame : nameToTreeVertexProp_[childVertexName].bag) {
      if (nVame == childVertexName) {
        continue;
      }
      auto nodeStep = nameToTreeVertexProp_[nVame].step;
      if (nodeStep < minStep) {
        minStep = nodeStep;
        parentVertexName = nVame;
      }
    }
    tree_.addEdge(parentVertexName, childVertexName);
    VLOG(4) << folly::format(
        "Added tree edge from {} to {}", parentVertexName, childVertexName);
  }
}

void
OccSolver::solveOcc() {
  std::vector<SubProblem> rootProblems;
  // Create left, right, and pick root subproblems
  rootProblems.push_back(SubProblem(rootVertex_, {rootVertex_}, {}, {}));
  rootProblems.push_back(SubProblem(rootVertex_, {}, {rootVertex_}, {}));
  rootProblems.push_back(SubProblem(rootVertex_, {}, {}, {rootVertex_}));

  // Push root problems to stack
  std::stack<SubProblem, std::vector<SubProblem>> problemsStack(rootProblems);

  while (!problemsStack.empty()) {
    SubProblem prob = problemsStack.top();
    problemsStack.pop();

    VLOG(4) << folly::format("Processing: {}", prob.sig_);

    // Check if all subproblems the problem depends on are available
    auto dep = prob.dependencies(this);
    bool allSubProblemsAvailable = true;
    for (const auto& depProb : dep) {
      auto subProbSig = depProb.sig_;
      if (!subProblems_.count(subProbSig)) {
        allSubProblemsAvailable = false;
        break;
      }
    }

    if (allSubProblemsAvailable) {
      // Identify best subproblem that problem depends on
      for (const auto& depProb : dep) {
        auto subProbSig = depProb.sig_;
        auto& subProb = subProblems_[subProbSig];
        if (!prob.chosen_.count(subProb.node_)) {
          prob.chosen_[subProb.node_] = subProbSig;
        } else {
          auto chosenProbSig = prob.chosen_[subProb.node_];
          auto& chosenProb = subProblems_[chosenProbSig];
          if (subProb.modifiedCost(this) < chosenProb.modifiedCost(this)) {
            prob.chosen_[subProb.node_] = subProbSig;
          }
        }
      }
      // compute cost of prob
      prob.cost_ = 0.0;
      for (const auto& uName : prob.pick_) {
        float vertexWeight = kDefaultVertexWeight;
        auto weightIt = vertexWeights_.find(uName);
        if (weightIt != vertexWeights_.end()) {
          vertexWeight = weightIt->second;
        }

        prob.cost_ += vertexWeight;
      }
      for (const auto& chosenIt : prob.chosen_) {
        auto sig = chosenIt.second;
        auto chosenProb = subProblems_[sig];
        prob.cost_ += chosenProb.modifiedCost(this);
      }
      // add prob to subproblems
      subProblems_[prob.sig_] = prob;
    } else {
      // create subproblems and add them to queue
      problemsStack.push(prob);
      VLOG(4) << folly::format("Push subproblem back to stack: {}", prob.sig_);
      for (const auto& subProb : dep) {
        problemsStack.push(subProb);
        VLOG(4)
            << folly::format("Push new subproblem to stack: {}", subProb.sig_);
      }
    }
  }
  // pick best root problem
  float minCost = std::numeric_limits<float>::max();
  for (const auto& rootProb : rootProblems) {
    auto prob = subProblems_[rootProb.sig_];
    if (prob.cost_ < minCost) {
      minCost = prob.cost_;
      minCostProb_ = prob;
    }
  }
  VLOG(3) << folly::format(
      "Best root problem is {} with cost {}",
      minCostProb_.sig_,
      minCostProb_.cost_);
}

std::unordered_set<std::string>
OccSolver::getOccSolution() {
  // Prepare tree decomposition of graph
  greedyTreeDecomp();
  // Solve the odd cycle cover problem
  solveOcc();

  // stack to keep track of problems that need processing
  // we start with optimal problem
  std::stack<SubProblem> problemsStack;
  problemsStack.push(minCostProb_);

  std::unordered_set<std::string> solution;
  while (!problemsStack.empty()) {
    auto prob = problemsStack.top();
    problemsStack.pop();

    solution.insert(prob.pick_.begin(), prob.pick_.end());
    for (const auto& probSigIt : prob.chosen_) {
      auto subProb = subProblems_[probSigIt.second];
      problemsStack.push(subProb);
    }
  }

  VLOG(2) << folly::format("OCC Solution is: {}", folly::join(", ", solution));
  return solution;
}

OccSolver::SubProblem::SubProblem(
    std::string node,
    std::set<std::string> left,
    std::set<std::string> write,
    std::set<std::string> pick)
    : node_(node), left_(left), right_(write), pick_(pick), cost_(0) {
  sig_ = signature();
}

std::string
OccSolver::SubProblem::signature() const {
  return folly::format(
             "({})=>({})({})({})",
             node_,
             folly::join(", ", left_),
             folly::join(", ", right_),
             folly::join(", ", pick_))
      .str();
}

float
OccSolver::SubProblem::modifiedCost(const OccSolver* parentHelper) const {
  auto ret = cost_;
  for (const auto& vName : pick_) {
    if (vName != node_) {
      float vertexWeight = kDefaultVertexWeight;
      auto weightIt = parentHelper->vertexWeights_.find(vName);
      if (weightIt != parentHelper->vertexWeights_.end()) {
        vertexWeight = weightIt->second;
      }

      ret -= vertexWeight;
    }
  }
  return ret;
}

std::vector<OccSolver::SubProblem>
OccSolver::SubProblem::dependencies(const OccSolver* parentHelper) {
  // Avoid recomputing dependencies
  if (depend_) {
    return depend_.value();
  }

  depend_ = std::vector<OccSolver::SubProblem>();
  auto children = parentHelper->tree_.getNeighbors(node_);
  for (const auto& child : children) {
    std::set<std::string> child_left;
    std::set<std::string> child_right;
    std::set<std::string> child_pick;
    std::unordered_set<std::string> bag;
    auto vPropIt = parentHelper->nameToTreeVertexProp_.find(child);
    if (vPropIt != parentHelper->nameToTreeVertexProp_.end()) {
      bag = vPropIt->second.bag;
    }

    for (const auto& node : bag) {
      if (left_.count(node)) {
        child_left.insert(node);
      }
      if (right_.count(node)) {
        child_right.insert(node);
      }
      if (pick_.count(node)) {
        child_pick.insert(node);
      }
    }

    auto neighbours = parentHelper->graph_.getNeighbors(child);
    bool connectedLeft = false;
    bool connectedRight = false;
    for (const auto& nbr : neighbours) {
      if (left_.count(nbr)) {
        connectedLeft = true;
      }
      if (right_.count(nbr)) {
        connectedRight = true;
      }
    }

    std::set<std::string> appendedWithChild;
    if (!connectedLeft) {
      appendedWithChild = child_left;
      appendedWithChild.insert(child);
      OccSolver::SubProblem lProb = OccSolver::SubProblem(
          child, appendedWithChild, child_right, child_pick);
      depend_->push_back(lProb);
    }
    if (!connectedRight) {
      appendedWithChild = child_right;
      appendedWithChild.insert(child);
      OccSolver::SubProblem rProb = OccSolver::SubProblem(
          child, child_left, appendedWithChild, child_pick);
      depend_->push_back(rProb);
    }
    appendedWithChild = child_pick;
    appendedWithChild.insert(child);
    OccSolver::SubProblem pProb = OccSolver::SubProblem(
        child, child_left, child_right, appendedWithChild);
    depend_->push_back(pProb);
  }
  return depend_.value();
}

} // namespace terragraph
} // namespace facebook
