#ifndef XGBOOST_TREE_UPDATER_PRUNE_INL_HPP_
#define XGBOOST_TREE_UPDATER_PRUNE_INL_HPP_
/*!
 * \file updater_prune-inl.hpp
 * \brief prune a tree given the statistics 
 * \author Tianqi Chen
 */
#include <vector>
#include "./param.h"
#include "./updater.h"
#include "../sync/sync.h"

namespace xgboost {
namespace tree {
/*! \brief pruner that prunes a tree after growing finishs */
class TreePruner: public IUpdater {
 public:
  virtual ~TreePruner(void) {}
  // set training parameter
  virtual void SetParam(const char *name, const char *val) {
    using namespace std;
    param.SetParam(name, val);
    if (!strcmp(name, "silent")) silent = atoi(val);
  }
  // update the tree, do pruning
  virtual void Update(const std::vector<bst_gpair> &gpair,
                      IFMatrix *p_fmat,
                      const BoosterInfo &info,
                      const std::vector<RegTree*> &trees) {
    // rescale learning rate according to size of trees
    float lr = param.learning_rate;
    param.learning_rate = lr / trees.size();
    for (size_t i = 0; i < trees.size(); ++i) {
      this->DoPrune(*trees[i]);
    }
    param.learning_rate = lr;
    this->SyncTrees(trees);
  }  
 private:
  // synchronize the trees in different nodes, take tree from rank 0
  inline void SyncTrees(const std::vector<RegTree *> &trees) {
    if (sync::GetWorldSize() == 1) return;
    std::string s_model;
    utils::MemoryBufferStream fs(&s_model);
    int rank = sync::GetRank();
    if (rank == 0) {
      for (size_t i = 0; i < trees.size(); ++i) {
        trees[i]->SaveModel(fs);
      }
      sync::Bcast(&s_model, 0);
    } else {
      sync::Bcast(&s_model, 0);
      for (size_t i = 0; i < trees.size(); ++i) {      
        trees[i]->LoadModel(fs);
      }
    }
  }
  // try to prune off current leaf
  inline int TryPruneLeaf(RegTree &tree, int nid, int depth, int npruned) {
    if (tree[nid].is_root()) return npruned;
    int pid = tree[nid].parent();
    RegTree::NodeStat &s = tree.stat(pid);
    ++s.leaf_child_cnt;
    if (s.leaf_child_cnt >= 2 && param.need_prune(s.loss_chg, depth - 1)) {
      // need to be pruned
      tree.ChangeToLeaf(pid, param.learning_rate * s.base_weight);
      // tail recursion
      return this->TryPruneLeaf(tree, pid, depth - 1, npruned+2);
    } else {
      return npruned;
    }    
  }
  /*! \brief do prunning of a tree */
  inline void DoPrune(RegTree &tree) {
    int npruned = 0;
    // initialize auxiliary statistics
    for (int nid = 0; nid < tree.param.num_nodes; ++nid) {
      tree.stat(nid).leaf_child_cnt = 0;
    }
    for (int nid = 0; nid < tree.param.num_nodes; ++nid) {
      if (tree[nid].is_leaf()) {
        npruned = this->TryPruneLeaf(tree, nid, tree.GetDepth(nid), npruned);
      }
    }
    if (silent == 0) {
      utils::Printf("tree prunning end, %d roots, %d extra nodes, %d pruned nodes ,max_depth=%d\n",
                    tree.param.num_roots, tree.num_extra_nodes(), npruned, tree.MaxDepth());
    }
  }

 private:
  // shutup
  int silent;
  // training parameter
  TrainParam param;
};
}  // namespace tree
}  // namespace xgboost
#endif  // XGBOOST_TREE_UPDATER_PRUNE_INL_HPP_
