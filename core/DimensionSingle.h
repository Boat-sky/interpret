// Copyright (c) 2018 Microsoft Corporation
// Licensed under the MIT license.
// Author: Paul Koch <code@koch.ninja>

#ifndef DIMENSION_SINGLE_H
#define DIMENSION_SINGLE_H

#include <type_traits> // std::is_standard_layout
#include <stddef.h> // size_t, ptrdiff_t

#include "EbmInternal.h" // EBM_INLINE
#include "Logging.h" // EBM_ASSERT & LOG
#include "SegmentedTensor.h"
#include "EbmStatistics.h"
#include "CachedThreadResources.h"
#include "FeatureCore.h"
#include "SamplingWithReplacement.h"
#include "HistogramBucket.h"

#include "TreeNode.h"

// TODO: it would be easy for us to implement a -1 lookback where we make the first cut, find the second cut, elimnate the first cut and try again on that side, then re-examine the second cut again.  For mains this would be very quick
//       we have found that 2-3 cuts are optimimum.  Probably 1 cut isn't very good since with 2 cuts we can localize a region of high gain in the center somewhere

template<bool bClassification>
struct SweepTreeNode {
   size_t m_cBestInstancesLeft;
   const HistogramBucket<bClassification> * m_pBestHistogramBucketEntry;
   
   // use the "struct hack" since Flexible array member method is not available in C++
   // m_aHistogramBucketVectorEntry must be the last item in this struct
   HistogramBucketVectorEntry<bClassification> m_aBestHistogramBucketVectorEntry[1];
};
template<bool bClassification>
EBM_INLINE bool GetSweepTreeNodeSizeOverflow(const size_t cVectorLength) {
   return IsMultiplyError(sizeof(HistogramBucketVectorEntry<bClassification>), cVectorLength) ? true : IsAddError(sizeof(SweepTreeNode<bClassification>) - sizeof(HistogramBucketVectorEntry<bClassification>), sizeof(HistogramBucketVectorEntry<bClassification>) * cVectorLength) ? true : false;
}
template<bool bClassification>
EBM_INLINE size_t GetSweepTreeNodeSize(const size_t cVectorLength) {
   return sizeof(SweepTreeNode<bClassification>) - sizeof(HistogramBucketVectorEntry<bClassification>) + sizeof(HistogramBucketVectorEntry<bClassification>) * cVectorLength;
}
template<bool bClassification>
EBM_INLINE SweepTreeNode<bClassification> * AddBytesSweepTreeNode(SweepTreeNode<bClassification> * const pSweepTreeNode, const size_t cBytesAdd) {
   return reinterpret_cast<SweepTreeNode<bClassification> *>(reinterpret_cast<char *>(pSweepTreeNode) + cBytesAdd);
}
template<bool bClassification>
EBM_INLINE size_t CountSweepTreeNode(const SweepTreeNode<bClassification> * const pSweepTreeNodeStart, const SweepTreeNode<bClassification> * const pSweepTreeNodeCur, const size_t cBytesPerSweepTreeNode) {
   EBM_ASSERT(reinterpret_cast<const char *>(pSweepTreeNodeStart) <= reinterpret_cast<const char *>(pSweepTreeNodeCur));
   const size_t cBytesDiff = reinterpret_cast<const char *>(pSweepTreeNodeCur) - reinterpret_cast<const char *>(pSweepTreeNodeStart);
   EBM_ASSERT(0 == cBytesDiff % cBytesPerSweepTreeNode);
   return cBytesDiff / cBytesPerSweepTreeNode;
}


// ExamineNodeForPossibleFutureSplittingAndDetermineBestSplitPoint can throw exceptions from the random number generator, possibly (it's not documented)
template<ptrdiff_t compilerLearningTypeOrCountTargetClasses>
bool ExamineNodeForPossibleFutureSplittingAndDetermineBestSplitPoint(RandomStream * const pRandomStream, const HistogramBucket<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const aHistogramBucket, TreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> * pTreeNode, CachedBoostingThreadResources<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const pCachedThreadResources, TreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const pTreeNodeChildrenAvailableStorageSpaceCur, const size_t cInstancesRequiredForChildSplitMin, const ptrdiff_t runtimeLearningTypeOrCountTargetClasses
#ifndef NDEBUG
   , const unsigned char * const aHistogramBucketsEndDebug
#endif // NDEBUG
) {
   LOG_N(TraceLevelVerbose, "Entered ExamineNodeForPossibleFutureSplittingAndDetermineBestSplitPoint: pRandomStream=%p, aHistogramBucket=%p, pTreeNode=%p, pCachedThreadResources=%p, pTreeNodeChildrenAvailableStorageSpaceCur=%p, cInstancesRequiredForChildSplitMin=%zu", static_cast<void *>(pRandomStream), static_cast<const void *>(aHistogramBucket), static_cast<void *>(pTreeNode), static_cast<void *>(pCachedThreadResources), static_cast<void *>(pTreeNodeChildrenAvailableStorageSpaceCur), cInstancesRequiredForChildSplitMin);

   const size_t cVectorLength = GET_VECTOR_LENGTH(compilerLearningTypeOrCountTargetClasses, runtimeLearningTypeOrCountTargetClasses);

   HistogramBucketVectorEntry<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const aSumHistogramBucketVectorEntryLeft = pCachedThreadResources->m_aSumHistogramBucketVectorEntry1;
   memset(aSumHistogramBucketVectorEntryLeft, 0, sizeof(*aSumHistogramBucketVectorEntryLeft) * cVectorLength);

   FractionalDataType * const aSumResidualErrorsRight = pCachedThreadResources->m_aSumResidualErrors2;
   for(size_t iVector = 0; iVector < cVectorLength; ++iVector) {
      aSumResidualErrorsRight[iVector] = ARRAY_TO_POINTER_CONST(pTreeNode->m_aHistogramBucketVectorEntry)[iVector].m_sumResidualError;
   }

   const HistogramBucket<IsClassification(compilerLearningTypeOrCountTargetClasses)> * pHistogramBucketEntryCur = pTreeNode->m_UNION.m_beforeExaminationForPossibleSplitting.m_pHistogramBucketEntryFirst;
   const HistogramBucket<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const pHistogramBucketEntryLast = pTreeNode->m_UNION.m_beforeExaminationForPossibleSplitting.m_pHistogramBucketEntryLast;

   EBM_ASSERT(!GetTreeNodeSizeOverflow<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cVectorLength)); // we're accessing allocated memory
   const size_t cBytesPerTreeNode = GetTreeNodeSize<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cVectorLength);

   TreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const pLeftChildInit = GetLeftTreeNodeChild<IsClassification(compilerLearningTypeOrCountTargetClasses)>(pTreeNodeChildrenAvailableStorageSpaceCur, cBytesPerTreeNode);
   pLeftChildInit->m_UNION.m_beforeExaminationForPossibleSplitting.m_pHistogramBucketEntryFirst = pHistogramBucketEntryCur;
   TreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const pRightChildInit = GetRightTreeNodeChild<IsClassification(compilerLearningTypeOrCountTargetClasses)>(pTreeNodeChildrenAvailableStorageSpaceCur, cBytesPerTreeNode);
   pRightChildInit->m_UNION.m_beforeExaminationForPossibleSplitting.m_pHistogramBucketEntryLast = pHistogramBucketEntryLast;

   EBM_ASSERT(!GetHistogramBucketSizeOverflow<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cVectorLength)); // we're accessing allocated memory
   const size_t cBytesPerHistogramBucket = GetHistogramBucketSize<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cVectorLength);
   EBM_ASSERT(!GetSweepTreeNodeSizeOverflow<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cVectorLength)); // we're accessing allocated memory
   const size_t cBytesPerSweepTreeNode = GetSweepTreeNodeSize<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cVectorLength);

   SweepTreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> * pSweepTreeNodeStart = static_cast<SweepTreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> *>(pCachedThreadResources->m_aEquivalentSplits);
   SweepTreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> * pSweepTreeNodeCur = pSweepTreeNodeStart;

   size_t cInstancesRight = pTreeNode->GetInstances();
   size_t cInstancesLeft = 0;
   FractionalDataType BEST_nodeSplittingScore = k_illegalGain;
#ifndef LEGACY_COMPATIBILITY
   EBM_ASSERT(0 < cInstancesRequiredForChildSplitMin);
#endif // LEGACY_COMPATIBILITY
   EBM_ASSERT(pHistogramBucketEntryLast != pHistogramBucketEntryCur); // we wouldn't call this function on a non-splittable node
   do {
      ASSERT_BINNED_BUCKET_OK(cBytesPerHistogramBucket, pHistogramBucketEntryCur, aHistogramBucketsEndDebug);

      const size_t CHANGE_cInstances = pHistogramBucketEntryCur->m_cInstancesInBucket;
      cInstancesRight -= CHANGE_cInstances;
      if(UNLIKELY(cInstancesRight < cInstancesRequiredForChildSplitMin)) {
         break; // we'll just keep subtracting if we continue, so there won't be any more splits, so we're done
      }
      cInstancesLeft += CHANGE_cInstances;
      if(LIKELY(cInstancesRequiredForChildSplitMin <= cInstancesLeft)) {
#ifndef LEGACY_COMPATIBILITY
         EBM_ASSERT(0 < cInstancesRight);
         EBM_ASSERT(0 < cInstancesLeft);
#endif // LEGACY_COMPATIBILITY

         const FractionalDataType cInstancesRightFractionalDataType = static_cast<FractionalDataType>(cInstancesRight);
         const FractionalDataType cInstancesLeftFractionalDataType = static_cast<FractionalDataType>(cInstancesLeft);
         FractionalDataType nodeSplittingScore = 0;
         for(size_t iVector = 0; iVector < cVectorLength; ++iVector) {
            const FractionalDataType CHANGE_sumResidualError = ARRAY_TO_POINTER_CONST(pHistogramBucketEntryCur->m_aHistogramBucketVectorEntry)[iVector].m_sumResidualError;

            const FractionalDataType sumResidualErrorRight = aSumResidualErrorsRight[iVector] - CHANGE_sumResidualError;
            aSumResidualErrorsRight[iVector] = sumResidualErrorRight;

            // TODO : we can make this faster by doing the division in ComputeNodeSplittingScore after we add all the numerators (but only do this after we've determined the best node splitting score for classification, and the NewtonRaphsonStep for gain
            const FractionalDataType nodeSplittingScoreRight = EbmStatistics::ComputeNodeSplittingScore(sumResidualErrorRight, cInstancesRightFractionalDataType);
            EBM_ASSERT(-0.00000001 <= nodeSplittingScoreRight);
            nodeSplittingScore += nodeSplittingScoreRight;

            const FractionalDataType sumResidualErrorLeft = aSumHistogramBucketVectorEntryLeft[iVector].m_sumResidualError + CHANGE_sumResidualError;
            aSumHistogramBucketVectorEntryLeft[iVector].m_sumResidualError = sumResidualErrorLeft;

            // TODO : we can make this faster by doing the division in ComputeNodeSplittingScore after we add all the numerators (but only do this after we've determined the best node splitting score for classification, and the NewtonRaphsonStep for gain
            const FractionalDataType nodeSplittingScoreLeft = EbmStatistics::ComputeNodeSplittingScore(sumResidualErrorLeft, cInstancesLeftFractionalDataType);
            EBM_ASSERT(-0.00000001 <= nodeSplittingScoreLeft);
            nodeSplittingScore += nodeSplittingScoreLeft;

            if(IsClassification(compilerLearningTypeOrCountTargetClasses)) {
               aSumHistogramBucketVectorEntryLeft[iVector].SetSumDenominator(aSumHistogramBucketVectorEntryLeft[iVector].GetSumDenominator() + ARRAY_TO_POINTER_CONST(pHistogramBucketEntryCur->m_aHistogramBucketVectorEntry)[iVector].GetSumDenominator());
            }
         }
         EBM_ASSERT(-0.000001 <= nodeSplittingScore);
         if(UNLIKELY(BEST_nodeSplittingScore <= nodeSplittingScore)) {
            // it's very possible that we have bins with zero instances in them, in which case we could easily be presented with equally favorable splits
            // or it's even possible for two different possible unrelated sections of bins, or individual bins to have exactly the same gain (think low count symetric data)
            // we want to avoid any bias of always choosing the higher or lower value to split on, so what we should do is store the indexes of any ties in a stack
            // and we reset the stack if we later find a gain that's larger than any we have in the stack.  The stack needs to be size_t to hold indexes, and we need
            // the stack to be as long as the number of instances - 1, incase all gain for all bins are the same (potential_splits = bins - 1)
            // after we exit the loop we can examine our stack and choose a random split from all the equivalent splits available
            // eg: we find that items at index 4,7,8,9 all have the same gain, so we pick a random number between 0 -> 3 to select which one we actually split on
            //
            // DON'T use a floating point epsilon when comparing the gains.  It's not clear what the epsilon should be given that gain is continuously pushed to
            // zero, so we can get very low numbers here eventually.  As an approximation, we should just take the assumption that if two numbers which have
            // mathematically equality, end up with different gains due to floating point computation issues, that the error will be roughtly symetric such that either
            // the first or the last could be chosen, which is fine for us since we just want to ensure randomized picking.
            // Having two mathematically identical gains is pretty rare in any case, except for the situation where one bucket has bins with zero instances, but in that
            // case we'll have floating point equality too since we'll be adding zero to the floating points values, which is an exact operation.
            //
            // TODO : implement the randomized splitting described for interaction effect, which can be done the same although we might want to include near matches since there is floating point noise there due to the way we sum interaction effect region totals

            pSweepTreeNodeCur = UNPREDICTABLE(BEST_nodeSplittingScore == nodeSplittingScore) ? pSweepTreeNodeCur : pSweepTreeNodeStart;
            BEST_nodeSplittingScore = nodeSplittingScore;

            pSweepTreeNodeCur->m_pBestHistogramBucketEntry = pHistogramBucketEntryCur;
            pSweepTreeNodeCur->m_cBestInstancesLeft = cInstancesLeft;
            memcpy(pSweepTreeNodeCur->m_aBestHistogramBucketVectorEntry, aSumHistogramBucketVectorEntryLeft, sizeof(*aSumHistogramBucketVectorEntryLeft) * cVectorLength);

            pSweepTreeNodeCur = AddBytesSweepTreeNode(pSweepTreeNodeCur, cBytesPerSweepTreeNode);
         }
      } else {
         for(size_t iVector = 0; iVector < cVectorLength; ++iVector) {
            const FractionalDataType CHANGE_sumResidualError = ARRAY_TO_POINTER_CONST(pHistogramBucketEntryCur->m_aHistogramBucketVectorEntry)[iVector].m_sumResidualError;

            aSumResidualErrorsRight[iVector] -= CHANGE_sumResidualError;
            aSumHistogramBucketVectorEntryLeft[iVector].m_sumResidualError += CHANGE_sumResidualError;
            if(IsClassification(compilerLearningTypeOrCountTargetClasses)) {
               aSumHistogramBucketVectorEntryLeft[iVector].SetSumDenominator(aSumHistogramBucketVectorEntryLeft[iVector].GetSumDenominator() + ARRAY_TO_POINTER_CONST(pHistogramBucketEntryCur->m_aHistogramBucketVectorEntry)[iVector].GetSumDenominator());
            }
         }
      }
      pHistogramBucketEntryCur = GetHistogramBucketByIndex<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cBytesPerHistogramBucket, pHistogramBucketEntryCur, 1);
   } while(pHistogramBucketEntryLast != pHistogramBucketEntryCur);

   if(UNLIKELY(pSweepTreeNodeStart == pSweepTreeNodeCur)) {
      // we didn't find any valid splits
      EBM_ASSERT(k_illegalGain == BEST_nodeSplittingScore);
      return true;
   }
   EBM_ASSERT(-0.0000001 <= BEST_nodeSplittingScore);

#ifdef LEGACY_COMPATIBILITY
   UNUSED(pRandomStream);
#else // LEGACY_COMPATIBILITY
   const size_t cSweepItems = CountSweepTreeNode(pSweepTreeNodeStart, pSweepTreeNodeCur, cBytesPerSweepTreeNode);
   if(UNLIKELY(1 < cSweepItems)) {
      // pRandomStream->Next(cSweepItems) can throw exceptions from the random number generator.  We would catch any exceptions in our caller
      const size_t iRandom = pRandomStream->Next(cSweepItems);
      pSweepTreeNodeStart = AddBytesSweepTreeNode(pSweepTreeNodeStart, cBytesPerSweepTreeNode * iRandom);
   }
#endif // LEGACY_COMPATIBILITY

   TreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const pLeftChild = GetLeftTreeNodeChild<IsClassification(compilerLearningTypeOrCountTargetClasses)>(pTreeNodeChildrenAvailableStorageSpaceCur, cBytesPerTreeNode);

   const HistogramBucket<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const BEST_pHistogramBucketEntry = pSweepTreeNodeStart->m_pBestHistogramBucketEntry;
   pLeftChild->m_UNION.m_beforeExaminationForPossibleSplitting.m_pHistogramBucketEntryLast = BEST_pHistogramBucketEntry;
   const size_t BEST_cInstancesLeft = pSweepTreeNodeStart->m_cBestInstancesLeft;
   pLeftChild->SetInstances(BEST_cInstancesLeft);

   const HistogramBucket<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const BEST_pHistogramBucketEntryNext = GetHistogramBucketByIndex<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cBytesPerHistogramBucket, BEST_pHistogramBucketEntry, 1);
   ASSERT_BINNED_BUCKET_OK(cBytesPerHistogramBucket, BEST_pHistogramBucketEntryNext, aHistogramBucketsEndDebug);

   TreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const pRightChild = GetRightTreeNodeChild<IsClassification(compilerLearningTypeOrCountTargetClasses)>(pTreeNodeChildrenAvailableStorageSpaceCur, cBytesPerTreeNode);

   pRightChild->m_UNION.m_beforeExaminationForPossibleSplitting.m_pHistogramBucketEntryFirst = BEST_pHistogramBucketEntryNext;
   const size_t cInstancesParent = pTreeNode->GetInstances();
   pRightChild->SetInstances(cInstancesParent - BEST_cInstancesLeft);

   const FractionalDataType cInstancesParentFractionalDataType = static_cast<FractionalDataType>(cInstancesParent);
#ifndef LEGACY_COMPATIBILITY
   // if the total instances is 0 then we should be using our specialty handling of that case
   // if the total instances if not 0, then our splitting code should never split any node that has zero on either the left or right, so no new parent should ever have zero instances
   EBM_ASSERT(0 < cInstancesParent);
#endif // LEGACY_COMPATIBILITY

   FractionalDataType originalParentScore = 0;
   for(size_t iVector = 0; iVector < cVectorLength; ++iVector) {
      const FractionalDataType BEST_sumResidualErrorLeft = pSweepTreeNodeStart->m_aBestHistogramBucketVectorEntry[iVector].m_sumResidualError;
      ARRAY_TO_POINTER(pLeftChild->m_aHistogramBucketVectorEntry)[iVector].m_sumResidualError = BEST_sumResidualErrorLeft;

      const FractionalDataType sumResidualErrorParent = ARRAY_TO_POINTER(pTreeNode->m_aHistogramBucketVectorEntry)[iVector].m_sumResidualError;
      ARRAY_TO_POINTER(pRightChild->m_aHistogramBucketVectorEntry)[iVector].m_sumResidualError = sumResidualErrorParent - BEST_sumResidualErrorLeft;

      originalParentScore += EbmStatistics::ComputeNodeSplittingScore(sumResidualErrorParent, cInstancesParentFractionalDataType);

      if(IsClassification(compilerLearningTypeOrCountTargetClasses)) {
         const FractionalDataType BEST_sumDenominatorLeft = pSweepTreeNodeStart->m_aBestHistogramBucketVectorEntry[iVector].GetSumDenominator();
         ARRAY_TO_POINTER(pLeftChild->m_aHistogramBucketVectorEntry)[iVector].SetSumDenominator(BEST_sumDenominatorLeft);
         ARRAY_TO_POINTER(pRightChild->m_aHistogramBucketVectorEntry)[iVector].SetSumDenominator(ARRAY_TO_POINTER(pTreeNode->m_aHistogramBucketVectorEntry)[iVector].GetSumDenominator() - BEST_sumDenominatorLeft);
      }
   }



   // IMPORTANT!! : we need to finish all our calls that use this->m_UNION.m_beforeExaminationForPossibleSplitting BEFORE setting anything in m_UNION.m_afterExaminationForPossibleSplitting as we do below this comment!  The call above to this->GetInstances() needs to be done above these lines because it uses m_UNION.m_beforeExaminationForPossibleSplitting for classification!



   pTreeNode->m_UNION.m_afterExaminationForPossibleSplitting.m_pTreeNodeChildren = pTreeNodeChildrenAvailableStorageSpaceCur;
   FractionalDataType splitGain = BEST_nodeSplittingScore - originalParentScore;
   EBM_ASSERT(-0.0000000001 <= splitGain); // within a set, no split should make our model worse.  It might in our validation set, but not within the training set
   pTreeNode->m_UNION.m_afterExaminationForPossibleSplitting.m_splitGain = splitGain;
#ifdef LEGACY_COMPATIBILITY
   pTreeNode->m_UNION.m_afterExaminationForPossibleSplitting.m_divisionValue = (BEST_pHistogramBucketEntry->m_bucketValue + BEST_pHistogramBucketEntryNext->m_bucketValue) / 2;
   UNUSED(aHistogramBucket);
#else // LEGACY_COMPATIBILITY
   EBM_ASSERT(reinterpret_cast<const char *>(aHistogramBucket) <= reinterpret_cast<const char *>(BEST_pHistogramBucketEntry));
   EBM_ASSERT(0 == (reinterpret_cast<const char *>(BEST_pHistogramBucketEntry) - reinterpret_cast<const char *>(aHistogramBucket)) % cBytesPerHistogramBucket);
   pTreeNode->m_UNION.m_afterExaminationForPossibleSplitting.m_divisionValue = (reinterpret_cast<const char *>(BEST_pHistogramBucketEntry) - reinterpret_cast<const char *>(aHistogramBucket)) / cBytesPerHistogramBucket;
#endif // LEGACY_COMPATIBILITY

   LOG_N(TraceLevelVerbose, "Exited ExamineNodeForPossibleFutureSplittingAndDetermineBestSplitPoint: divisionValue=%zu, nodeSplittingScore=%" FractionalDataTypePrintf, static_cast<size_t>(pTreeNode->m_UNION.m_afterExaminationForPossibleSplitting.m_divisionValue), pTreeNode->m_UNION.m_afterExaminationForPossibleSplitting.m_splitGain);

   return false;
}

template<ptrdiff_t compilerLearningTypeOrCountTargetClasses>
bool GrowDecisionTree(RandomStream * const pRandomStream, CachedBoostingThreadResources<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const pCachedThreadResources, const ptrdiff_t runtimeLearningTypeOrCountTargetClasses, const size_t cHistogramBuckets, const HistogramBucket<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const aHistogramBucket, const size_t cInstancesTotal, const HistogramBucketVectorEntry<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const aSumHistogramBucketVectorEntry, const size_t cTreeSplitsMax, const size_t cInstancesRequiredForParentSplitMin, const size_t cInstancesRequiredForChildSplitMin, SegmentedTensor<ActiveDataType, FractionalDataType> * const pSmallChangeToModelOverwriteSingleSamplingSet, FractionalDataType * const pTotalGain
#ifndef NDEBUG
   , const unsigned char * const aHistogramBucketsEndDebug
#endif // NDEBUG
) {
   LOG_0(TraceLevelVerbose, "Entered GrowDecisionTree");

   const size_t cVectorLength = GET_VECTOR_LENGTH(compilerLearningTypeOrCountTargetClasses, runtimeLearningTypeOrCountTargetClasses);

   EBM_ASSERT(nullptr != pTotalGain);
   EBM_ASSERT(1 <= cInstancesTotal); // filter these out at the start where we can handle this case easily
   EBM_ASSERT(1 <= cHistogramBuckets); // cHistogramBuckets could only be zero if cInstancesTotal.  We should filter out that special case at our entry point though!!
   // TODO: I think we can eliminate the check for 1 == cHistogramBuckets once we get ride of bucket compresssion (on zeros)
   // TODO: do we already have a separate solution for no splits, which we could use for 0 == cTreeSplitsMax
   if(UNLIKELY(cInstancesTotal < cInstancesRequiredForParentSplitMin || 1 == cHistogramBuckets || 0 == cTreeSplitsMax)) {
      // there will be no splits at all

      // TODO : this section can probably be eliminated in the future when we disable cInstancesRequiredForParentSplitMin, and butcket comrpession (making 2 <= cHistogramBuckets), and 0 == cTreeSplitsMax can be handled by using our non-splitting specialty boosting function

   no_splits:;

      if(UNLIKELY(pSmallChangeToModelOverwriteSingleSamplingSet->SetCountDivisions(0, 0))) {
         LOG_0(TraceLevelWarning, "WARNING GrowDecisionTree pSmallChangeToModelOverwriteSingleSamplingSet->SetCountDivisions(0, 0)");
         return true;
      }

      // we don't need to call EnsureValueCapacity because by default we start with a value capacity of 2 * cVectorLength

      if(IsClassification(compilerLearningTypeOrCountTargetClasses)) {
         FractionalDataType * const aValues = pSmallChangeToModelOverwriteSingleSamplingSet->GetValuePointer();
         for(size_t iVector = 0; iVector < cVectorLength; ++iVector) {
            FractionalDataType smallChangeToModel = EbmStatistics::ComputeSmallChangeInClassificationLogOddPredictionForOneSegment(aSumHistogramBucketVectorEntry[iVector].m_sumResidualError, aSumHistogramBucketVectorEntry[iVector].GetSumDenominator());
            aValues[iVector] = smallChangeToModel;
         }
      } else {
         EBM_ASSERT(IsRegression(compilerLearningTypeOrCountTargetClasses));
         const FractionalDataType smallChangeToModel = EbmStatistics::ComputeSmallChangeInRegressionPredictionForOneSegment(aSumHistogramBucketVectorEntry[0].m_sumResidualError, static_cast<FractionalDataType>(cInstancesTotal));
         FractionalDataType * pValues = pSmallChangeToModelOverwriteSingleSamplingSet->GetValuePointer();
         pValues[0] = smallChangeToModel;
      }

      LOG_0(TraceLevelVerbose, "Exited GrowDecisionTree via not enough data to split");
      *pTotalGain = 0;
      return false;
   }

   // there will be at least one split

   if(GetTreeNodeSizeOverflow<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cVectorLength)) {
      LOG_0(TraceLevelWarning, "WARNING GrowDecisionTree GetTreeNodeSizeOverflow<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cVectorLength)");
      return true; // we haven't accessed this TreeNode memory yet, so we don't know if it overflows yet
   }
   const size_t cBytesPerTreeNode = GetTreeNodeSize<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cVectorLength);
   EBM_ASSERT(!GetHistogramBucketSizeOverflow<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cVectorLength)); // we're accessing allocated memory
   const size_t cBytesPerHistogramBucket = GetHistogramBucketSize<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cVectorLength);

retry_with_bigger_tree_node_children_array:
   size_t cBytesBuffer2 = pCachedThreadResources->GetThreadByteBuffer2Size();
   const size_t cBytesInitialNeededAllocation = 3 * cBytesPerTreeNode; // we need 1 TreeNode for the root, 1 for the left child of the root and 1 for the right child of the root
   if(cBytesBuffer2 < cBytesInitialNeededAllocation) {
      // TODO : we can eliminate this check as long as we ensure that the ThreadByteBuffer2 is always initialized to be equal to the size of three TreeNodes (left and right) == GET_SIZEOF_ONE_TREE_NODE_CHILDREN(cBytesPerTreeNode), or the number of bins (interactions multiply bins) on the highest bin count feature
      if(pCachedThreadResources->GrowThreadByteBuffer2(cBytesInitialNeededAllocation)) {
         LOG_0(TraceLevelWarning, "WARNING GrowDecisionTree pCachedThreadResources->GrowThreadByteBuffer2(cBytesInitialNeededAllocation)");
         return true;
      }
      cBytesBuffer2 = pCachedThreadResources->GetThreadByteBuffer2Size();
      EBM_ASSERT(cBytesInitialNeededAllocation <= cBytesBuffer2);
   }
   TreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> * pRootTreeNode = static_cast<TreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> *>(pCachedThreadResources->GetThreadByteBuffer2());

   pRootTreeNode->m_UNION.m_beforeExaminationForPossibleSplitting.m_pHistogramBucketEntryFirst = aHistogramBucket;
   pRootTreeNode->m_UNION.m_beforeExaminationForPossibleSplitting.m_pHistogramBucketEntryLast = GetHistogramBucketByIndex<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cBytesPerHistogramBucket, aHistogramBucket, cHistogramBuckets - 1);
   ASSERT_BINNED_BUCKET_OK(cBytesPerHistogramBucket, pRootTreeNode->m_UNION.m_beforeExaminationForPossibleSplitting.m_pHistogramBucketEntryLast, aHistogramBucketsEndDebug);
   pRootTreeNode->SetInstances(cInstancesTotal);

   memcpy(ARRAY_TO_POINTER(pRootTreeNode->m_aHistogramBucketVectorEntry), aSumHistogramBucketVectorEntry, cVectorLength * sizeof(*aSumHistogramBucketVectorEntry)); // copying existing mem

   size_t cSplits;
   try {
      // ExamineNodeForPossibleFutureSplittingAndDetermineBestSplitPoint can throw exceptions from the random number generator, possibly (it's not documented)
      if(ExamineNodeForPossibleFutureSplittingAndDetermineBestSplitPoint<compilerLearningTypeOrCountTargetClasses>(pRandomStream, aHistogramBucket, pRootTreeNode, pCachedThreadResources, AddBytesTreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)>(pRootTreeNode, cBytesPerTreeNode), cInstancesRequiredForChildSplitMin, runtimeLearningTypeOrCountTargetClasses
#ifndef NDEBUG
         , aHistogramBucketsEndDebug
#endif // NDEBUG
         )) {
         goto no_splits;
      }

      if(UNPREDICTABLE(PREDICTABLE(1 == cTreeSplitsMax) || UNPREDICTABLE(2 == cHistogramBuckets))) {
         // there will be exactly 1 split, which is a special case that we can return faster without as much overhead as the multiple split case

         EBM_ASSERT(2 != cHistogramBuckets || !GetLeftTreeNodeChild<IsClassification(compilerLearningTypeOrCountTargetClasses)>(pRootTreeNode->m_UNION.m_afterExaminationForPossibleSplitting.m_pTreeNodeChildren, cBytesPerTreeNode)->IsSplittable(cInstancesRequiredForParentSplitMin) && !GetRightTreeNodeChild<IsClassification(compilerLearningTypeOrCountTargetClasses)>(pRootTreeNode->m_UNION.m_afterExaminationForPossibleSplitting.m_pTreeNodeChildren, cBytesPerTreeNode)->IsSplittable(cInstancesRequiredForParentSplitMin));

         if(UNLIKELY(pSmallChangeToModelOverwriteSingleSamplingSet->SetCountDivisions(0, 1))) {
            LOG_0(TraceLevelWarning, "WARNING GrowDecisionTree pSmallChangeToModelOverwriteSingleSamplingSet->SetCountDivisions(0, 1)");
            return true;
         }

         ActiveDataType * pDivisions = pSmallChangeToModelOverwriteSingleSamplingSet->GetDivisionPointer(0);
         pDivisions[0] = pRootTreeNode->m_UNION.m_afterExaminationForPossibleSplitting.m_divisionValue;

         // we don't need to call EnsureValueCapacity because by default we start with a value capacity of 2 * cVectorLength

         // TODO : we don't need to get the right and left pointer from the root.. we know where they will be
         const TreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const pLeftChild = GetLeftTreeNodeChild<IsClassification(compilerLearningTypeOrCountTargetClasses)>(pRootTreeNode->m_UNION.m_afterExaminationForPossibleSplitting.m_pTreeNodeChildren, cBytesPerTreeNode);
         const TreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const pRightChild = GetRightTreeNodeChild<IsClassification(compilerLearningTypeOrCountTargetClasses)>(pRootTreeNode->m_UNION.m_afterExaminationForPossibleSplitting.m_pTreeNodeChildren, cBytesPerTreeNode);

         FractionalDataType * const aValues = pSmallChangeToModelOverwriteSingleSamplingSet->GetValuePointer();
         if(IsClassification(compilerLearningTypeOrCountTargetClasses)) {
            for(size_t iVector = 0; iVector < cVectorLength; ++iVector) {
               aValues[iVector] = EbmStatistics::ComputeSmallChangeInClassificationLogOddPredictionForOneSegment(ARRAY_TO_POINTER_CONST(pLeftChild->m_aHistogramBucketVectorEntry)[iVector].m_sumResidualError, ARRAY_TO_POINTER_CONST(pLeftChild->m_aHistogramBucketVectorEntry)[iVector].GetSumDenominator());
               aValues[cVectorLength + iVector] = EbmStatistics::ComputeSmallChangeInClassificationLogOddPredictionForOneSegment(ARRAY_TO_POINTER_CONST(pRightChild->m_aHistogramBucketVectorEntry)[iVector].m_sumResidualError, ARRAY_TO_POINTER_CONST(pRightChild->m_aHistogramBucketVectorEntry)[iVector].GetSumDenominator());
            }
         } else {
            EBM_ASSERT(IsRegression(compilerLearningTypeOrCountTargetClasses));
            aValues[0] = EbmStatistics::ComputeSmallChangeInRegressionPredictionForOneSegment(ARRAY_TO_POINTER_CONST(pLeftChild->m_aHistogramBucketVectorEntry)[0].m_sumResidualError, static_cast<FractionalDataType>(pLeftChild->GetInstances()));
            aValues[1] = EbmStatistics::ComputeSmallChangeInRegressionPredictionForOneSegment(ARRAY_TO_POINTER_CONST(pRightChild->m_aHistogramBucketVectorEntry)[0].m_sumResidualError, static_cast<FractionalDataType>(pRightChild->GetInstances()));
         }

         LOG_0(TraceLevelVerbose, "Exited GrowDecisionTree via one tree split");
         *pTotalGain = pRootTreeNode->EXTRACT_GAIN_BEFORE_SPLITTING();
         return false;
      }

      // it's very likely that there will be more than 1 split below this point.  The only case where we wouldn't split below is if both our children nodes dont't have enough cases
      // to split, but that should rare

      // typically we train on stumps, so often this priority queue is overhead since with 2-3 splits the overhead is too large to benefit, 
      // but we also aren't bottlenecked if we only have 2-3 splits, so we don't care about performance issues.  On the other hand, we don't want
      // to change this to an array scan because in theory the user can specify very deep trees, and we don't want to hang on an O(N^2) operation
      // if they do.  So, let's keep the priority queue, and only the priority queue since it handles all scenarios without any real cost and is simpler
      // than implementing an optional array scan PLUS a priority queue for deep trees.
      std::priority_queue<TreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> *, std::vector<TreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> *>, CompareTreeNodeSplittingGain<IsClassification(compilerLearningTypeOrCountTargetClasses)>> * pBestTreeNodeToSplit = &pCachedThreadResources->m_bestTreeNodeToSplit.m_queue;

      // it is ridiculous that we need to do this in order to clear the tree (there is no "clear" function), but inside this queue is a chunk of memory, and we want to ensure that the chunk of memory stays in L1 cache, so we pop all the previous garbage off instead of allocating a new one!
      while(!pBestTreeNodeToSplit->empty()) {
         pBestTreeNodeToSplit->pop();
      }

      cSplits = 0;
      TreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> * pParentTreeNode = pRootTreeNode;

      // we skip 3 tree nodes.  The root, the left child of the root, and the right child of the root
      TreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> * pTreeNodeChildrenAvailableStorageSpaceCur = AddBytesTreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)>(pRootTreeNode, cBytesInitialNeededAllocation);

      FractionalDataType totalGain = 0;

      goto skip_first_push_pop;

      do {
         // there is no way to get the top and pop at the same time.. would be good to get a better queue, but our code isn't bottlenecked by it
         pParentTreeNode = pBestTreeNodeToSplit->top();
         // In theory we can have nodes with equal gain values here, but this is very very rare to occur in practice
         // We handle equal gain values in ExamineNodeForPossibleFutureSplittingAndDetermineBestSplitPoint because we 
         // can have zero instnaces in bins, in which case it occurs, but those equivalent situations have been cleansed by
         // the time we reach this code, so the only realistic scenario where we might get equivalent gains is if we had an almost
         // symetric distribution instances bin distributions AND two tail ends that happen to have the same statistics AND
         // either this is our first cut, or we've only made a single cut in the center in the case where there is symetry in the center
         // Even if all of these things are true, after one non-symetric cut, we won't see that scenario anymore since the residuals won't be
         // symetric anymore.  This is so rare, and limited to one cut, so we shouldn't bother to handle it since the complexity of doing so
         // outweights the benefits.
         pBestTreeNodeToSplit->pop();

      skip_first_push_pop:

         // ONLY AFTER WE'VE POPPED pParentTreeNode OFF the priority queue is it considered to have been split.  Calling SPLIT_THIS_NODE makes it formal
         totalGain += pParentTreeNode->EXTRACT_GAIN_BEFORE_SPLITTING();
         pParentTreeNode->SPLIT_THIS_NODE();

         TreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const pLeftChild = GetLeftTreeNodeChild<IsClassification(compilerLearningTypeOrCountTargetClasses)>(pParentTreeNode->m_UNION.m_afterExaminationForPossibleSplitting.m_pTreeNodeChildren, cBytesPerTreeNode);
         if(pLeftChild->IsSplittable(cInstancesRequiredForParentSplitMin)) {
            TreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> * pTreeNodeChildrenAvailableStorageSpaceNext = AddBytesTreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)>(pTreeNodeChildrenAvailableStorageSpaceCur, cBytesPerTreeNode << 1);
            if(cBytesBuffer2 < static_cast<size_t>(reinterpret_cast<char *>(pTreeNodeChildrenAvailableStorageSpaceNext) - reinterpret_cast<char *>(pRootTreeNode))) {
               if(pCachedThreadResources->GrowThreadByteBuffer2(cBytesPerTreeNode)) {
                  LOG_0(TraceLevelWarning, "WARNING GrowDecisionTree pCachedThreadResources->GrowThreadByteBuffer2(cBytesPerTreeNode)");
                  return true;
               }
               goto retry_with_bigger_tree_node_children_array;
            }
            // the act of splitting it implicitly sets INDICATE_THIS_NODE_EXAMINED_FOR_SPLIT_AND_REJECTED because splitting sets splitGain to a non-illegalGain value
            // ExamineNodeForPossibleFutureSplittingAndDetermineBestSplitPoint can throw exceptions from the random number generator, possibly (it's not documented)
            if(!ExamineNodeForPossibleFutureSplittingAndDetermineBestSplitPoint<compilerLearningTypeOrCountTargetClasses>(pRandomStream, aHistogramBucket, pLeftChild, pCachedThreadResources, pTreeNodeChildrenAvailableStorageSpaceCur, cInstancesRequiredForChildSplitMin, runtimeLearningTypeOrCountTargetClasses
#ifndef NDEBUG
               , aHistogramBucketsEndDebug
#endif // NDEBUG
               )) {
               pTreeNodeChildrenAvailableStorageSpaceCur = pTreeNodeChildrenAvailableStorageSpaceNext;
               pBestTreeNodeToSplit->push(pLeftChild);
            } else {
               goto no_left_split;
            }
         } else {
         no_left_split:;
            // we aren't going to split this TreeNode because we can't.  We need to set the splitGain value here because otherwise it is filled with garbage that could be NaN (meaning the node was a branch)
            // we can't call INDICATE_THIS_NODE_EXAMINED_FOR_SPLIT_AND_REJECTED before calling SplitTreeNode because INDICATE_THIS_NODE_EXAMINED_FOR_SPLIT_AND_REJECTED sets m_UNION.m_afterExaminationForPossibleSplitting.m_splitGain and the m_UNION.m_beforeExaminationForPossibleSplitting values are needed if we had decided to call ExamineNodeForSplittingAndDetermineBestPossibleSplit
            pLeftChild->INDICATE_THIS_NODE_EXAMINED_FOR_SPLIT_AND_REJECTED();
         }

         TreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const pRightChild = GetRightTreeNodeChild<IsClassification(compilerLearningTypeOrCountTargetClasses)>(pParentTreeNode->m_UNION.m_afterExaminationForPossibleSplitting.m_pTreeNodeChildren, cBytesPerTreeNode);
         if(pRightChild->IsSplittable(cInstancesRequiredForParentSplitMin)) {
            TreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)> * pTreeNodeChildrenAvailableStorageSpaceNext = AddBytesTreeNode<IsClassification(compilerLearningTypeOrCountTargetClasses)>(pTreeNodeChildrenAvailableStorageSpaceCur, cBytesPerTreeNode << 1);
            if(cBytesBuffer2 < static_cast<size_t>(reinterpret_cast<char *>(pTreeNodeChildrenAvailableStorageSpaceNext) - reinterpret_cast<char *>(pRootTreeNode))) {
               if(pCachedThreadResources->GrowThreadByteBuffer2(cBytesPerTreeNode)) {
                  LOG_0(TraceLevelWarning, "WARNING GrowDecisionTree pCachedThreadResources->GrowThreadByteBuffer2(cBytesPerTreeNode)");
                  return true;
               }
               goto retry_with_bigger_tree_node_children_array;
            }
            // the act of splitting it implicitly sets INDICATE_THIS_NODE_EXAMINED_FOR_SPLIT_AND_REJECTED because splitting sets splitGain to a non-NaN value
            // ExamineNodeForPossibleFutureSplittingAndDetermineBestSplitPoint can throw exceptions from the random number generator, possibly (it's not documented)
            if(!ExamineNodeForPossibleFutureSplittingAndDetermineBestSplitPoint<compilerLearningTypeOrCountTargetClasses>(pRandomStream, aHistogramBucket, pRightChild, pCachedThreadResources, pTreeNodeChildrenAvailableStorageSpaceCur, cInstancesRequiredForChildSplitMin, runtimeLearningTypeOrCountTargetClasses
#ifndef NDEBUG
               , aHistogramBucketsEndDebug
#endif // NDEBUG
               )) {
               pTreeNodeChildrenAvailableStorageSpaceCur = pTreeNodeChildrenAvailableStorageSpaceNext;
               pBestTreeNodeToSplit->push(pRightChild);
            } else {
               goto no_right_split;
            }
         } else {
         no_right_split:;
            // we aren't going to split this TreeNode because we can't.  We need to set the splitGain value here because otherwise it is filled with garbage that could be NaN (meaning the node was a branch)
            // we can't call INDICATE_THIS_NODE_EXAMINED_FOR_SPLIT_AND_REJECTED before calling SplitTreeNode because INDICATE_THIS_NODE_EXAMINED_FOR_SPLIT_AND_REJECTED sets m_UNION.m_afterExaminationForPossibleSplitting.m_splitGain and the m_UNION.m_beforeExaminationForPossibleSplitting values are needed if we had decided to call ExamineNodeForSplittingAndDetermineBestPossibleSplit
            pRightChild->INDICATE_THIS_NODE_EXAMINED_FOR_SPLIT_AND_REJECTED();
         }
         ++cSplits;
      } while(cSplits < cTreeSplitsMax && UNLIKELY(!pBestTreeNodeToSplit->empty()));
      // we DON'T need to call SetLeafAfterDone() on any items that remain in the pBestTreeNodeToSplit queue because everything in that queue has set a non-NaN nodeSplittingScore value

      // we might as well dump this value out to our pointer, even if later fail the function below.  If the function is failed, we make no guarantees about what we did with the value pointed to at *pTotalGain
      *pTotalGain = totalGain;
      EBM_ASSERT(static_cast<size_t>(reinterpret_cast<char *>(pTreeNodeChildrenAvailableStorageSpaceCur) - reinterpret_cast<char *>(pRootTreeNode)) <= cBytesBuffer2);
   } catch(...) {
      // ExamineNodeForPossibleFutureSplittingAndDetermineBestSplitPoint can throw exceptions from the random number generator, possibly (it's not documented)
      // calling anything inside pBestTreeNodeToSplit can throw exceptions, possibly (it's not documented)
      LOG_0(TraceLevelWarning, "WARNING GrowDecisionTree exception");
      return true;
   }

   if(UNLIKELY(pSmallChangeToModelOverwriteSingleSamplingSet->SetCountDivisions(0, cSplits))) {
      LOG_0(TraceLevelWarning, "WARNING GrowDecisionTree pSmallChangeToModelOverwriteSingleSamplingSet->SetCountDivisions(0, cSplits)");
      return true;
   }
   if(IsMultiplyError(cVectorLength, cSplits + 1)) {
      LOG_0(TraceLevelWarning, "WARNING GrowDecisionTree IsMultiplyError(cVectorLength, cSplits + 1)");
      return true;
   }
   if(UNLIKELY(pSmallChangeToModelOverwriteSingleSamplingSet->EnsureValueCapacity(cVectorLength * (cSplits + 1)))) {
      LOG_0(TraceLevelWarning, "WARNING GrowDecisionTree pSmallChangeToModelOverwriteSingleSamplingSet->EnsureValueCapacity(cVectorLength * (cSplits + 1)");
      return true;
   }
   ActiveDataType * pDivisions = pSmallChangeToModelOverwriteSingleSamplingSet->GetDivisionPointer(0);
   FractionalDataType * pValues = pSmallChangeToModelOverwriteSingleSamplingSet->GetValuePointer();

   LOG_0(TraceLevelVerbose, "Entered Flatten");
   pRootTreeNode->Flatten(&pDivisions, &pValues, cVectorLength);
   LOG_0(TraceLevelVerbose, "Exited Flatten");

   EBM_ASSERT(pSmallChangeToModelOverwriteSingleSamplingSet->GetDivisionPointer(0) <= pDivisions);
   EBM_ASSERT(static_cast<size_t>(pDivisions - pSmallChangeToModelOverwriteSingleSamplingSet->GetDivisionPointer(0)) == cSplits);
   EBM_ASSERT(pSmallChangeToModelOverwriteSingleSamplingSet->GetValuePointer() < pValues);
   EBM_ASSERT(static_cast<size_t>(pValues - pSmallChangeToModelOverwriteSingleSamplingSet->GetValuePointer()) == cVectorLength * (cSplits + 1));

   LOG_0(TraceLevelVerbose, "Exited GrowDecisionTree via normal exit");
   return false;
}

// TODO : make call parameter ordering consistent with BinDataSet call below (put the feature first since that's a definition that happens before the training data set)
template<ptrdiff_t compilerLearningTypeOrCountTargetClasses>
bool BoostZeroDimensional(CachedBoostingThreadResources<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const pCachedThreadResources, const SamplingMethod * const pTrainingSet, SegmentedTensor<ActiveDataType, FractionalDataType> * const pSmallChangeToModelOverwriteSingleSamplingSet, const ptrdiff_t runtimeLearningTypeOrCountTargetClasses) {
   LOG_0(TraceLevelVerbose, "Entered BoostZeroDimensional");

   const size_t cVectorLength = GET_VECTOR_LENGTH(compilerLearningTypeOrCountTargetClasses, runtimeLearningTypeOrCountTargetClasses);
   if(GetHistogramBucketSizeOverflow<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cVectorLength)) {
      // TODO : move this to initialization where we execute it only once
      LOG_0(TraceLevelWarning, "GetHistogramBucketSizeOverflow<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cVectorLength)");
      return true;
   }
   const size_t cBytesPerHistogramBucket = GetHistogramBucketSize<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cVectorLength);
   HistogramBucket<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const pHistogramBucket = static_cast<HistogramBucket<IsClassification(compilerLearningTypeOrCountTargetClasses)> *>(pCachedThreadResources->GetThreadByteBuffer1(cBytesPerHistogramBucket));
   if(UNLIKELY(nullptr == pHistogramBucket)) {
      LOG_0(TraceLevelWarning, "WARNING nullptr == pHistogramBucket");
      return true;
   }
   memset(pHistogramBucket, 0, cBytesPerHistogramBucket);

   BinDataSetTrainingZeroDimensions<compilerLearningTypeOrCountTargetClasses>(pHistogramBucket, pTrainingSet, runtimeLearningTypeOrCountTargetClasses);

   const HistogramBucketVectorEntry<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const aSumHistogramBucketVectorEntry = ARRAY_TO_POINTER(pHistogramBucket->m_aHistogramBucketVectorEntry);
   if(IsClassification(compilerLearningTypeOrCountTargetClasses)) {
      FractionalDataType * aValues = pSmallChangeToModelOverwriteSingleSamplingSet->GetValuePointer();
      for(size_t iVector = 0; iVector < cVectorLength; ++iVector) {
         FractionalDataType smallChangeToModel = EbmStatistics::ComputeSmallChangeInClassificationLogOddPredictionForOneSegment(aSumHistogramBucketVectorEntry[iVector].m_sumResidualError, aSumHistogramBucketVectorEntry[iVector].GetSumDenominator());
         aValues[iVector] = smallChangeToModel;
      }
   } else {
      EBM_ASSERT(IsRegression(compilerLearningTypeOrCountTargetClasses));
      FractionalDataType smallChangeToModel = EbmStatistics::ComputeSmallChangeInRegressionPredictionForOneSegment(aSumHistogramBucketVectorEntry[0].m_sumResidualError, static_cast<FractionalDataType>(pHistogramBucket->m_cInstancesInBucket));
      FractionalDataType * pValues = pSmallChangeToModelOverwriteSingleSamplingSet->GetValuePointer();
      pValues[0] = smallChangeToModel;
   }

   LOG_0(TraceLevelVerbose, "Exited BoostZeroDimensional");
   return false;
}

// TODO : make call parameter ordering consistent with BinDataSet call below (put the feature first since that's a definition that happens before the training data set)
template<ptrdiff_t compilerLearningTypeOrCountTargetClasses>
bool BoostSingleDimensional(RandomStream * const pRandomStream, CachedBoostingThreadResources<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const pCachedThreadResources, const SamplingMethod * const pTrainingSet, const FeatureCombinationCore * const pFeatureCombination, const size_t cTreeSplitsMax, const size_t cInstancesRequiredForParentSplitMin, const size_t cInstancesRequiredForChildSplitMin, SegmentedTensor<ActiveDataType, FractionalDataType> * const pSmallChangeToModelOverwriteSingleSamplingSet, FractionalDataType * const pTotalGain, const ptrdiff_t runtimeLearningTypeOrCountTargetClasses) {
   LOG_0(TraceLevelVerbose, "Entered BoostSingleDimensional");

   EBM_ASSERT(1 == pFeatureCombination->m_cFeatures);
   size_t cTotalBuckets = ARRAY_TO_POINTER_CONST(pFeatureCombination->m_FeatureCombinationEntry)[0].m_pFeature->m_cBins;

   const size_t cVectorLength = GET_VECTOR_LENGTH(compilerLearningTypeOrCountTargetClasses, runtimeLearningTypeOrCountTargetClasses);
   if(GetHistogramBucketSizeOverflow<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cVectorLength)) {
      // TODO : move this to initialization where we execute it only once
      LOG_0(TraceLevelWarning, "WARNING GetHistogramBucketSizeOverflow<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cVectorLength)");
      return true;
   }
   const size_t cBytesPerHistogramBucket = GetHistogramBucketSize<IsClassification(compilerLearningTypeOrCountTargetClasses)>(cVectorLength);
   if(IsMultiplyError(cTotalBuckets, cBytesPerHistogramBucket)) {
      // TODO : move this to initialization where we execute it only once
      LOG_0(TraceLevelWarning, "WARNING IsMultiplyError(cTotalBuckets, cBytesPerHistogramBucket)");
      return true;
   }
   const size_t cBytesBuffer = cTotalBuckets * cBytesPerHistogramBucket;
   HistogramBucket<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const aHistogramBuckets = static_cast<HistogramBucket<IsClassification(compilerLearningTypeOrCountTargetClasses)> *>(pCachedThreadResources->GetThreadByteBuffer1(cBytesBuffer));
   if(UNLIKELY(nullptr == aHistogramBuckets)) {
      LOG_0(TraceLevelWarning, "WARNING BoostSingleDimensional nullptr == aHistogramBuckets");
      return true;
   }
   // !!! VERY IMPORTANT: zero our one extra bucket for BuildFastTotals to use for multi-dimensional !!!!
   memset(aHistogramBuckets, 0, cBytesBuffer);

#ifndef NDEBUG
   const unsigned char * const aHistogramBucketsEndDebug = reinterpret_cast<unsigned char *>(aHistogramBuckets) + cBytesBuffer;
#endif // NDEBUG

   BinDataSetTraining<compilerLearningTypeOrCountTargetClasses, 1>(aHistogramBuckets, pFeatureCombination, pTrainingSet, runtimeLearningTypeOrCountTargetClasses
#ifndef NDEBUG
      , aHistogramBucketsEndDebug
#endif // NDEBUG
   );

   HistogramBucketVectorEntry<IsClassification(compilerLearningTypeOrCountTargetClasses)> * const aSumHistogramBucketVectorEntry = pCachedThreadResources->m_aSumHistogramBucketVectorEntry;
   memset(aSumHistogramBucketVectorEntry, 0, sizeof(*aSumHistogramBucketVectorEntry) * cVectorLength); // can't overflow, accessing existing memory

   size_t cHistogramBuckets = ARRAY_TO_POINTER_CONST(pFeatureCombination->m_FeatureCombinationEntry)[0].m_pFeature->m_cBins;
   EBM_ASSERT(1 <= cHistogramBuckets); // this function can handle 1 == cBins even though that's a degenerate case that shouldn't be boosted on (dimensions with 1 bin don't contribute anything since they always have the same value)
   size_t cInstancesTotal;
   cHistogramBuckets = CompressHistogramBuckets<compilerLearningTypeOrCountTargetClasses>(pTrainingSet, cHistogramBuckets, aHistogramBuckets, &cInstancesTotal, aSumHistogramBucketVectorEntry, runtimeLearningTypeOrCountTargetClasses
#ifndef NDEBUG
      , aHistogramBucketsEndDebug
#endif // NDEBUG
   );

   EBM_ASSERT(1 <= cInstancesTotal);
   EBM_ASSERT(1 <= cHistogramBuckets);

   bool bRet = GrowDecisionTree<compilerLearningTypeOrCountTargetClasses>(pRandomStream, pCachedThreadResources, runtimeLearningTypeOrCountTargetClasses, cHistogramBuckets, aHistogramBuckets, cInstancesTotal, aSumHistogramBucketVectorEntry, cTreeSplitsMax, cInstancesRequiredForParentSplitMin, cInstancesRequiredForChildSplitMin, pSmallChangeToModelOverwriteSingleSamplingSet, pTotalGain
#ifndef NDEBUG
      , aHistogramBucketsEndDebug
#endif // NDEBUG
   );

   LOG_0(TraceLevelVerbose, "Exited BoostSingleDimensional");
   return bRet;
}

#endif // DIMENSION_SINGLE_H
