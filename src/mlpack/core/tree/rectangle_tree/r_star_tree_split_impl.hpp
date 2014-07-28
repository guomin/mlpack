/**
 * @file r_star_tree_split_impl.hpp
 * @author Andrew Wells
 *
 * Implementation of class (RStarTreeSplit) to split a RectangleTree.
 */
#ifndef __MLPACK_CORE_TREE_RECTANGLE_TREE_R_STAR_TREE_SPLIT_IMPL_HPP
#define __MLPACK_CORE_TREE_RECTANGLE_TREE_R_STAR_TREE_SPLIT_IMPL_HPP

#include "r_star_tree_split.hpp"
#include "rectangle_tree.hpp"
#include <mlpack/core/math/range.hpp>

namespace mlpack {
namespace tree {

/**
 * We call GetPointSeeds to get the two points which will be the initial points in the new nodes
 * We then call AssignPointDestNode to assign the remaining points to the two new nodes.
 * Finally, we delete the old node and insert the new nodes into the tree, spliting the parent
 * if necessary.
 */
template<typename DescentType,
typename StatisticType,
typename MatType>
void RStarTreeSplit<DescentType, StatisticType, MatType>::SplitLeafNode(
        RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType>* tree,
        std::vector<bool>& relevels)
{
  // If we are splitting the root node, we need will do things differently so that the constructor
  // and other methods don't confuse the end user by giving an address of another node.
  if (tree->Parent() == NULL) {
    RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType>* copy = 
        new RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType>(*tree, false); // We actually want to copy this way.  Pointers and everything.

    copy->Parent() = tree;
    tree->Count() = 0;
    tree->NullifyData();
    tree->Children()[(tree->NumChildren())++] = copy; // Because this was a leaf node, numChildren must be 0.
    assert(tree->NumChildren() == 1);
    RStarTreeSplit<DescentType, StatisticType, MatType>::SplitLeafNode(copy, relevels);
    return;
  }
   
 // If we haven't yet reinserted on this level, we try doing so now.
 if(relevels[tree->TreeDepth()]) {
   relevels[tree->TreeDepth()] = false;
   // We sort the points by decreasing distance to the centroid of the bound.
   // We then remove the first p entries and reinsert them at the root.
   RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType>* root = tree;
   while(root->Parent() != NULL)
     root = root->Parent();
   size_t p = tree->MaxLeafSize() * 0.3; // The paper says this works the best.
   if(p == 0) {
     SplitLeafNode(tree, relevels);
     return;
   }
   
   std::vector<sortStruct> sorted(tree->Count());
   arma::vec centroid;
   tree->Bound().Centroid(centroid); // Modifies centroid.
   for(size_t i = 0; i < sorted.size(); i++) {
     sorted[i].d = tree->Bound().Metric().Evaluate(centroid, tree->LocalDataset().col(i));
     sorted[i].n = i;
   }
   
   std::sort(sorted.begin(), sorted.end(), structComp);
   std::vector<int> pointIndices(p);
   for(size_t i = 0; i < p; i++) {
     pointIndices[i] = tree->Points()[sorted[sorted.size()-1-i].n]; // We start from the end of sorted.
     root->DeletePoint(tree->Points()[sorted[sorted.size()-1-i].n], relevels);
   }
   for(size_t i = 0; i < p; i++) { // We reverse the order again to reinsert the closest points first.
     root->InsertPoint(pointIndices[p-1-i], relevels);
   }
   
//    // If we went below min fill, delete this node and reinsert all points.
//    if(tree->Count() < tree->MinLeafSize()) {
//      std::vector<int> pointIndices(tree->Count());
//      for(size_t i = 0; i < tree->Count(); i++) {
//        pointIndices[i] = tree->Points()[i];
//      }
//      root->RemoveNode(tree, relevels);
//      for(size_t i = 0; i < pointIndices.size(); i++) {
//        root->InsertPoint(pointIndices[i], relevels);
//      }
//      //tree->SoftDelete();      
//    }
   return;
 }

  int bestOverlapIndexOnBestAxis = 0;
  int bestAreaIndexOnBestAxis = 0;
  bool tiedOnOverlap = false;
  int bestAxis = 0;
  double bestAxisScore = DBL_MAX;
  for (int j = 0; j < tree->Bound().Dim(); j++) {
    double axisScore = 0.0;
    // Since we only have points in the leaf nodes, we only need to sort once.
    std::vector<sortStruct> sorted(tree->Count());
    for (int i = 0; i < sorted.size(); i++) {
      sorted[i].d = tree->LocalDataset().col(i)[j];
      sorted[i].n = i;
    }

    std::sort(sorted.begin(), sorted.end(), structComp);

    // We'll store each of the three scores for each distribution.
    std::vector<double> areas(tree->MaxLeafSize() - 2 * tree->MinLeafSize() + 2);
    std::vector<double> margins(tree->MaxLeafSize() - 2 * tree->MinLeafSize() + 2);
    std::vector<double> overlapedAreas(tree->MaxLeafSize() - 2 * tree->MinLeafSize() + 2);
    for (int i = 0; i < areas.size(); i++) {
      areas[i] = 0.0;
      margins[i] = 0.0;
      overlapedAreas[i] = 0.0;
    }
    for (int i = 0; i < areas.size(); i++) {
      // The ith arrangement is obtained by placing the first tree->MinLeafSize() + i 
      // points in one rectangle and the rest in another.  Then we calculate the three
      // scores for that distribution.

      int cutOff = tree->MinLeafSize() + i;
      // We'll calculate the max and min in each dimension by hand to save time.
      std::vector<double> maxG1(tree->Bound().Dim());
      std::vector<double> minG1(maxG1.size());
      std::vector<double> maxG2(maxG1.size());
      std::vector<double> minG2(maxG1.size());
      for (int k = 0; k < tree->Bound().Dim(); k++) {
        minG1[k] = maxG1[k] = tree->LocalDataset().col(sorted[0].n)[k];
        minG2[k] = maxG2[k] = tree->LocalDataset().col(sorted[sorted.size() - 1].n)[k];
        for (int l = 1; l < tree->Count() - 1; l++) {
          if (l < cutOff) {
            if (tree->LocalDataset().col(sorted[l].n)[k] < minG1[k])
              minG1[k] = tree->LocalDataset().col(sorted[l].n)[k];
            else if (tree->LocalDataset().col(sorted[l].n)[k] > maxG1[k])
              maxG1[k] = tree->LocalDataset().col(sorted[l].n)[k];
          } else {
            if (tree->LocalDataset().col(sorted[l].n)[k] < minG2[k])
              minG2[k] = tree->LocalDataset().col(sorted[l].n)[k];
            else if (tree->LocalDataset().col(sorted[l].n)[k] > maxG2[k])
              maxG2[k] = tree->LocalDataset().col(sorted[l].n)[k];
          }
        }
      }
      double area1 = 1.0, area2 = 1.0;
      double oArea = 1.0;
      for (int k = 0; k < maxG1.size(); k++) {
        margins[i] += maxG1[k] - minG1[k] + maxG2[k] - minG2[k];
        area1 *= maxG1[k] - minG1[k];
        area2 *= maxG2[k] - minG2[k];
        oArea *= maxG1[k] < minG2[k] || maxG2[k] < minG1[k] ? 0.0 : std::min(maxG1[k], maxG2[k]) - std::max(minG1[k], minG2[k]);
      }
      areas[i] += area1 + area2;
      overlapedAreas[i] += oArea;
      axisScore += margins[i];
    }

    if (axisScore < bestAxisScore) {
      bestAxisScore = axisScore;
      bestAxis = j;
      bestOverlapIndexOnBestAxis = 0;
      bestAreaIndexOnBestAxis = 0;
      for (int i = 1; i < areas.size(); i++) {
        if (overlapedAreas[i] < overlapedAreas[bestOverlapIndexOnBestAxis]) {
          tiedOnOverlap = false;
          bestAreaIndexOnBestAxis = i;
          bestOverlapIndexOnBestAxis = i;
        } else if (overlapedAreas[i] == overlapedAreas[bestOverlapIndexOnBestAxis]) {
          tiedOnOverlap = true;
          if (areas[i] < areas[bestAreaIndexOnBestAxis])
            bestAreaIndexOnBestAxis = i;
        }
      }
    }
  }

  std::vector<sortStruct> sorted(tree->Count());
  for (int i = 0; i < sorted.size(); i++) {
    sorted[i].d = tree->LocalDataset().col(i)[bestAxis];
    sorted[i].n = i;
  }

  std::sort(sorted.begin(), sorted.end(), structComp);

  RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType> *treeOne = new
          RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType>(tree->Parent());
  RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType> *treeTwo = new
          RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType>(tree->Parent());

  if (tiedOnOverlap) {
    for (int i = 0; i < tree->Count(); i++) {
      if (i < bestAreaIndexOnBestAxis + tree->MinLeafSize())
        treeOne->InsertPoint(tree->Points()[sorted[i].n]);
      else
        treeTwo->InsertPoint(tree->Points()[sorted[i].n]);
    }
  } else {
    for (int i = 0; i < tree->Count(); i++) {
      if (i < bestOverlapIndexOnBestAxis + tree->MinLeafSize())
        treeOne->InsertPoint(tree->Points()[sorted[i].n]);
      else
        treeTwo->InsertPoint(tree->Points()[sorted[i].n]);
    }
  }  

  //Remove this node and insert treeOne and treeTwo
  RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType>* par = tree->Parent();
  int index = -1;
  for (int i = 0; i < par->NumChildren(); i++) {
    if (par->Children()[i] == tree) {
      index = i;
      break;
    }
  }
  assert(index != -1);
  par->Children()[index] = treeOne;
  par->Children()[par->NumChildren()++] = treeTwo;

  // we only add one at a time, so we should only need to test for equality
  // just in case, we use an assert.
  assert(par->NumChildren() <= par->MaxNumChildren()+1);
  if (par->NumChildren() == par->MaxNumChildren()+1) {
    SplitNonLeafNode(par, relevels);
  }

  assert(treeOne->Parent()->NumChildren() <= treeOne->MaxNumChildren());
  assert(treeOne->Parent()->NumChildren() >= treeOne->MinNumChildren());
  assert(treeTwo->Parent()->NumChildren() <= treeTwo->MaxNumChildren());
  assert(treeTwo->Parent()->NumChildren() >= treeTwo->MinNumChildren());

  tree->SoftDelete();

  return;
}

/**
 * We call GetBoundSeeds to get the two new nodes that this one will be broken
 * into.  Then we call AssignNodeDestNode to move the children of this node
 * into either of those two nodes.  Finally, we delete the now unused information
 * and recurse up the tree if necessary.  We don't need to worry about the bounds
 * higher up the tree because they were already updated if necessary.
 */
template<typename DescentType,
typename StatisticType,
typename MatType>
bool RStarTreeSplit<DescentType, StatisticType, MatType>::SplitNonLeafNode(
        RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType>* tree,
        std::vector<bool>& relevels)
{
  // If we are splitting the root node, we need will do things differently so that the constructor
  // and other methods don't confuse the end user by giving an address of another node.
  if (tree->Parent() == NULL) {
    RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType>* copy = 
        new RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType>(*tree, false); // We actually want to copy this way.  Pointers and everything.

    copy->Parent() = tree;
    tree->NumChildren() = 0;
    tree->NullifyData();
    tree->Children()[(tree->NumChildren())++] = copy;
    RStarTreeSplit<DescentType, StatisticType, MatType>::SplitNonLeafNode(copy, relevels);
    return true;
  }

  // If we haven't yet reinserted on this level, we try doing so now.
  if(relevels[tree->TreeDepth()]) {
    relevels[tree->TreeDepth()] = false;
    // We sort the points by decreasing centroid to centroid distance.
    // We then remove the first p entries and reinsert them at the root.
    RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType>* root = tree;
    while(root->Parent() != NULL)
      root = root->Parent();
    size_t p = tree->MaxNumChildren() * 0.3; // The paper says this works the best.
    if(p == 0) {
      SplitNonLeafNode(tree, relevels);
      return false;
    }
    
    std::vector<sortStruct> sorted(tree->NumChildren());
    arma::vec c1;
    tree->Bound().Centroid(c1); // Modifies c1.
    for(size_t i = 0; i < sorted.size(); i++) {
      arma::vec c2;
      tree->Children()[i]->Bound().Centroid(c2); // Modifies c2.
      sorted[i].d = tree->Bound().Metric().Evaluate(c1,c2);
      sorted[i].n = i;
    }
    std::sort(sorted.begin(), sorted.end(), structComp);
    
    //bool startBelowMinFill = tree->NumChildren() < tree->MinNumChildren();
 
    std::vector<RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType>*> removedNodes(p);
    for(size_t i =0; i < p; i++) {
      removedNodes[i] = tree->Children()[sorted[sorted.size()-1-i].n]; // We start from the end of sorted.
      root->RemoveNode(tree->Children()[sorted[sorted.size()-1-i].n], relevels);
    }
    
    for(size_t i = 0; i < p; i++) {
      root->InsertNode(removedNodes[p-1-i], tree->TreeDepth(), relevels); // We reverse the order again to reinsert the closest nodes first.
    }
    
    // If we went below min fill, delete this node and reinsert all children.
    //SOMETHING IS WRONG.  SHOULD NOT GO BELOW MIN FILL.
//    if(!startBelowMinFill && tree->NumChildren() < tree->MinNumChildren())
//    std::cout<<"MINFILLERROR "<< p << ", " << tree->NumChildren() << "; " << tree->MaxNumChildren()<<std::endl;
    
//    if(tree->NumChildren() < tree->MinNumChildren()) {
//      std::vector<RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType>*> rmNodes(tree->NumChildren());
//      for(size_t i = 0; i < rmNodes.size(); i++) {
//        rmNodes[i] = tree->Children()[i];
//      }
//      root->RemoveNode(tree, relevels);
//      for(size_t i = 0; i < rmNodes.size(); i++) {
//        root->InsertNode(rmNodes[i], tree->TreeDepth(), relevels);
//      }
// //      tree->SoftDelete();
//    }    
 //    assert(tree->NumChildren() >= tree->MinNumChildren());
 //    assert(tree->NumChildren() <= tree->MaxNumChildren());
   
   return false;
 }

  int bestOverlapIndexOnBestAxis = 0;
  int bestAreaIndexOnBestAxis = 0;
  bool tiedOnOverlap = false;
  bool lowIsBest = true;
  int bestAxis = 0;
  double bestAxisScore = DBL_MAX;
  for (int j = 0; j < tree->Bound().Dim(); j++) {
    double axisScore = 0.0;

    // We'll do Bound().Lo() now and use Bound().Hi() later.
    std::vector<sortStruct> sorted(tree->NumChildren());
    for (int i = 0; i < sorted.size(); i++) {
      sorted[i].d = tree->Children()[i]->Bound()[j].Lo();
      sorted[i].n = i;
    }

    std::sort(sorted.begin(), sorted.end(), structComp);

    // We'll store each of the three scores for each distribution.
    std::vector<double> areas(tree->MaxNumChildren() - 2 * tree->MinNumChildren() + 2);
    std::vector<double> margins(tree->MaxNumChildren() - 2 * tree->MinNumChildren() + 2);
    std::vector<double> overlapedAreas(tree->MaxNumChildren() - 2 * tree->MinNumChildren() + 2);
    for (int i = 0; i < areas.size(); i++) {
      areas[i] = 0.0;
      margins[i] = 0.0;
      overlapedAreas[i] = 0.0;
    }

    for (int i = 0; i < areas.size(); i++) {
      // The ith arrangement is obtained by placing the first tree->MinNumChildren() + i 
      // points in one rectangle and the rest in another.  Then we calculate the three
      // scores for that distribution.

      int cutOff = tree->MinNumChildren() + i;
      // We'll calculate the max and min in each dimension by hand to save time.
      std::vector<double> maxG1(tree->Bound().Dim());
      std::vector<double> minG1(maxG1.size());
      std::vector<double> maxG2(maxG1.size());
      std::vector<double> minG2(maxG1.size());
      for (int k = 0; k < tree->Bound().Dim(); k++) {
        minG1[k] = tree->Children()[sorted[0].n]->Bound()[k].Lo();
        maxG1[k] = tree->Children()[sorted[0].n]->Bound()[k].Hi();
        minG2[k] = tree->Children()[sorted[sorted.size() - 1].n]->Bound()[k].Lo();
        maxG2[k] = tree->Children()[sorted[sorted.size() - 1].n]->Bound()[k].Hi();
        for (int l = 1; l < tree->NumChildren() - 1; l++) {
          if (l < cutOff) {
            if (tree->Children()[sorted[l].n]->Bound()[k].Lo() < minG1[k])
              minG1[k] = tree->Children()[sorted[l].n]->Bound()[k].Lo();
            else if (tree->Children()[sorted[l].n]->Bound()[k].Hi() > maxG1[k])
              maxG1[k] = tree->Children()[sorted[l].n]->Bound()[k].Hi();
          } else {
            if (tree->Children()[sorted[l].n]->Bound()[k].Lo() < minG2[k])
              minG2[k] = tree->Children()[sorted[l].n]->Bound()[k].Lo();
            else if (tree->Children()[sorted[l].n]->Bound()[k].Hi() > maxG2[k])
              maxG2[k] = tree->Children()[sorted[l].n]->Bound()[k].Hi();
          }
        }
      }
      double area1 = 1.0, area2 = 1.0;
      double oArea = 1.0;
      for (int k = 0; k < maxG1.size(); k++) {
        margins[i] += maxG1[k] - minG1[k] + maxG2[k] - minG2[k];
        area1 *= maxG1[k] - minG1[k];
        area2 *= maxG2[k] - minG2[k];
        oArea *= maxG1[k] < minG2[k] || maxG2[k] < minG1[k] ? 0.0 : std::min(maxG1[k], maxG2[k]) - std::max(minG1[k], minG2[k]);
      }
      areas[i] += area1 + area2;
      overlapedAreas[i] += oArea;
      axisScore += margins[i];
    }
    if (axisScore < bestAxisScore) {
      bestAxisScore = axisScore;
      bestAxis = j;
      double bestOverlapIndexOnBestAxis = 0;
      double bestAreaIndexOnBestAxis = 0;
      for (int i = 1; i < areas.size(); i++) {
        if (overlapedAreas[i] < overlapedAreas[bestOverlapIndexOnBestAxis]) {
          tiedOnOverlap = false;
          bestAreaIndexOnBestAxis = i;
          bestOverlapIndexOnBestAxis = i;
        } else if (overlapedAreas[i] == overlapedAreas[bestOverlapIndexOnBestAxis]) {
          tiedOnOverlap = true;
          if (areas[i] < areas[bestAreaIndexOnBestAxis])
            bestAreaIndexOnBestAxis = i;
        }
      }
    }
  }

  //Now we do the same thing using Bound().Hi() and choose the best of the two.
  for (int j = 0; j < tree->Bound().Dim(); j++) {
    double axisScore = 0.0;

    // We'll do Bound().Lo() now and use Bound().Hi() later.
    std::vector<sortStruct> sorted(tree->NumChildren());
    for (int i = 0; i < sorted.size(); i++) {
      sorted[i].d = tree->Children()[i]->Bound()[j].Hi();
      sorted[i].n = i;
    }

    std::sort(sorted.begin(), sorted.end(), structComp);

    // We'll store each of the three scores for each distribution.
    std::vector<double> areas(tree->MaxNumChildren() - 2 * tree->MinNumChildren() + 2);
    std::vector<double> margins(tree->MaxNumChildren() - 2 * tree->MinNumChildren() + 2);
    std::vector<double> overlapedAreas(tree->MaxNumChildren() - 2 * tree->MinNumChildren() + 2);
    for (int i = 0; i < areas.size(); i++) {
      areas[i] = 0.0;
      margins[i] = 0.0;
      overlapedAreas[i] = 0.0;
    }

    for (int i = 0; i < areas.size(); i++) {
      // The ith arrangement is obtained by placing the first tree->MinNumChildren() + i 
      // points in one rectangle and the rest in another.  Then we calculate the three
      // scores for that distribution.

      int cutOff = tree->MinNumChildren() + i;
      // We'll calculate the max and min in each dimension by hand to save time.
      std::vector<double> maxG1(tree->Bound().Dim());
      std::vector<double> minG1(maxG1.size());
      std::vector<double> maxG2(maxG1.size());
      std::vector<double> minG2(maxG1.size());
      for (int k = 0; k < tree->Bound().Dim(); k++) {
        minG1[k] = tree->Children()[sorted[0].n]->Bound()[k].Lo();
        maxG1[k] = tree->Children()[sorted[0].n]->Bound()[k].Hi();
        minG2[k] = tree->Children()[sorted[sorted.size() - 1].n]->Bound()[k].Lo();
        maxG2[k] = tree->Children()[sorted[sorted.size() - 1].n]->Bound()[k].Hi();
        for (int l = 1; l < tree->NumChildren() - 1; l++) {
          if (l < cutOff) {
            if (tree->Children()[sorted[l].n]->Bound()[k].Lo() < minG1[k])
              minG1[k] = tree->Children()[sorted[l].n]->Bound()[k].Lo();
            else if (tree->Children()[sorted[l].n]->Bound()[k].Hi() > maxG1[k])
              maxG1[k] = tree->Children()[sorted[l].n]->Bound()[k].Hi();
          } else {
            if (tree->Children()[sorted[l].n]->Bound()[k].Lo() < minG2[k])
              minG2[k] = tree->Children()[sorted[l].n]->Bound()[k].Lo();
            else if (tree->Children()[sorted[l].n]->Bound()[k].Hi() > maxG2[k])
              maxG2[k] = tree->Children()[sorted[l].n]->Bound()[k].Hi();
          }
        }
      }
      double area1 = 1.0, area2 = 1.0;
      double oArea = 1.0;
      for (int k = 0; k < maxG1.size(); k++) {
        margins[i] += maxG1[k] - minG1[k] + maxG2[k] - minG2[k];
        area1 *= maxG1[k] - minG1[k];
        area2 *= maxG2[k] - minG2[k];
        oArea *= maxG1[k] < minG2[k] || maxG2[k] < minG1[k] ? 0.0 : std::min(maxG1[k], maxG2[k]) - std::max(minG1[k], minG2[k]);
      }
      areas[i] += area1 + area2;
      overlapedAreas[i] += oArea;
      axisScore += margins[i];
    }
    if (axisScore < bestAxisScore) {
      bestAxisScore = axisScore;
      bestAxis = j;
      lowIsBest = false;
      double bestOverlapIndexOnBestAxis = 0;
      double bestAreaIndexOnBestAxis = 0;
      for (int i = 1; i < areas.size(); i++) {
        if (overlapedAreas[i] < overlapedAreas[bestOverlapIndexOnBestAxis]) {
          tiedOnOverlap = false;
          bestAreaIndexOnBestAxis = i;
          bestOverlapIndexOnBestAxis = i;
        } else if (overlapedAreas[i] == overlapedAreas[bestOverlapIndexOnBestAxis]) {
          tiedOnOverlap = true;
          if (areas[i] < areas[bestAreaIndexOnBestAxis])
            bestAreaIndexOnBestAxis = i;
        }
      }
    }
  }

  std::vector<sortStruct> sorted(tree->NumChildren());
  if (lowIsBest) {
    for (int i = 0; i < sorted.size(); i++) {
      sorted[i].d = tree->Children()[i]->Bound()[bestAxis].Lo();
      sorted[i].n = i;
    }
  } else {
    for (int i = 0; i < sorted.size(); i++) {
      sorted[i].d = tree->Children()[i]->Bound()[bestAxis].Hi();
      sorted[i].n = i;
    }
  }

  std::sort(sorted.begin(), sorted.end(), structComp);

  RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType> *treeOne = new
          RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType>(tree->Parent());
  RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType> *treeTwo = new
          RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType>(tree->Parent());

  if (tiedOnOverlap) {
    for (int i = 0; i < tree->NumChildren(); i++) {
      if (i < bestAreaIndexOnBestAxis + tree->MinNumChildren())
        InsertNodeIntoTree(treeOne, tree->Children()[sorted[i].n]);
      else
        InsertNodeIntoTree(treeTwo, tree->Children()[sorted[i].n]);
    }
  } else {
    for (int i = 0; i < tree->NumChildren(); i++) {
      if (i < bestOverlapIndexOnBestAxis + tree->MinNumChildren())
        InsertNodeIntoTree(treeOne, tree->Children()[sorted[i].n]);
      else
        InsertNodeIntoTree(treeTwo, tree->Children()[sorted[i].n]);
    }
  }

  //Remove this node and insert treeOne and treeTwo
  RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType>* par = tree->Parent();
  int index = 0;
  for (int i = 0; i < par->NumChildren(); i++) {
    if (par->Children()[i] == tree) {
      index = i;
      break;
    }
  }
  par->Children()[index] = treeOne;
  par->Children()[par->NumChildren()++] = treeTwo;

  // we only add one at a time, so we should only need to test for equality
  // just in case, we use an assert.
  assert(par->NumChildren() <= par->MaxNumChildren()+1);
  if (par->NumChildren() == par->MaxNumChildren()+1) {
    SplitNonLeafNode(par, relevels);
  }
  
  // We have to update the children of each of these new nodes so that they record the 
  // correct parent.
  for (int i = 0; i < treeOne->NumChildren(); i++) {
    treeOne->Children()[i]->Parent() = treeOne;
  }
  for (int i = 0; i < treeTwo->NumChildren(); i++) {
    treeTwo->Children()[i]->Parent() = treeTwo;
  }

  assert(treeOne->Parent()->NumChildren() <= treeOne->MaxNumChildren());
  assert(treeOne->Parent()->NumChildren() >= treeOne->MinNumChildren());
  assert(treeTwo->Parent()->NumChildren() <= treeTwo->MaxNumChildren());
  assert(treeTwo->Parent()->NumChildren() >= treeTwo->MinNumChildren());
  
  assert(treeOne->MaxNumChildren() < 7);
  assert(treeTwo->MaxNumChildren() < 7);

  tree->SoftDelete();
  
  return false;
}

/**
 * Insert a node into another node.  Expanding the bounds and updating the numberOfChildren.
 */
template<typename DescentType,
typename StatisticType,
typename MatType>
void RStarTreeSplit<DescentType, StatisticType, MatType>::InsertNodeIntoTree(
        RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType>* destTree,
        RectangleTree<RStarTreeSplit<DescentType, StatisticType, MatType>, DescentType, StatisticType, MatType>* srcNode)
{
  destTree->Bound() |= srcNode->Bound();
  destTree->Children()[destTree->NumChildren()++] = srcNode;
}

}; // namespace tree
}; // namespace mlpack

#endif
