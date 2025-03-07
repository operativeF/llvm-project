//===-- VPlanTransforms.cpp - Utility VPlan to VPlan transforms -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a set of utility VPlan to VPlan transformations.
///
//===----------------------------------------------------------------------===//

#include "VPlanTransforms.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/IVDescriptors.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Intrinsics.h"

using namespace llvm;

void VPlanTransforms::VPInstructionsToVPRecipes(
    Loop *OrigLoop, VPlanPtr &Plan,
    function_ref<const InductionDescriptor *(PHINode *)>
        GetIntOrFpInductionDescriptor,
    SmallPtrSetImpl<Instruction *> &DeadInstructions, ScalarEvolution &SE,
    const TargetLibraryInfo &TLI) {

  ReversePostOrderTraversal<VPBlockRecursiveTraversalWrapper<VPBlockBase *>>
      RPOT(Plan->getEntry());
  for (VPBasicBlock *VPBB : VPBlockUtils::blocksOnly<VPBasicBlock>(RPOT)) {
    VPRecipeBase *Term = VPBB->getTerminator();
    auto EndIter = Term ? Term->getIterator() : VPBB->end();
    // Introduce each ingredient into VPlan.
    for (VPRecipeBase &Ingredient :
         make_early_inc_range(make_range(VPBB->begin(), EndIter))) {

      VPValue *VPV = Ingredient.getVPSingleValue();
      Instruction *Inst = cast<Instruction>(VPV->getUnderlyingValue());
      if (DeadInstructions.count(Inst)) {
        VPValue DummyValue;
        VPV->replaceAllUsesWith(&DummyValue);
        Ingredient.eraseFromParent();
        continue;
      }

      VPRecipeBase *NewRecipe = nullptr;
      if (auto *VPPhi = dyn_cast<VPWidenPHIRecipe>(&Ingredient)) {
        auto *Phi = cast<PHINode>(VPPhi->getUnderlyingValue());
        if (const auto *II = GetIntOrFpInductionDescriptor(Phi)) {
          VPValue *Start = Plan->getOrAddVPValue(II->getStartValue());
          VPValue *Step =
              vputils::getOrCreateVPValueForSCEVExpr(*Plan, II->getStep(), SE);
          NewRecipe =
              new VPWidenIntOrFpInductionRecipe(Phi, Start, Step, *II, true);
        } else {
          Plan->addVPValue(Phi, VPPhi);
          continue;
        }
      } else {
        assert(isa<VPInstruction>(&Ingredient) &&
               "only VPInstructions expected here");
        assert(!isa<PHINode>(Inst) && "phis should be handled above");
        // Create VPWidenMemoryInstructionRecipe for loads and stores.
        if (LoadInst *Load = dyn_cast<LoadInst>(Inst)) {
          NewRecipe = new VPWidenMemoryInstructionRecipe(
              *Load, Plan->getOrAddVPValue(getLoadStorePointerOperand(Inst)),
              nullptr /*Mask*/, false /*Consecutive*/, false /*Reverse*/);
        } else if (StoreInst *Store = dyn_cast<StoreInst>(Inst)) {
          NewRecipe = new VPWidenMemoryInstructionRecipe(
              *Store, Plan->getOrAddVPValue(getLoadStorePointerOperand(Inst)),
              Plan->getOrAddVPValue(Store->getValueOperand()), nullptr /*Mask*/,
              false /*Consecutive*/, false /*Reverse*/);
        } else if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Inst)) {
          NewRecipe = new VPWidenGEPRecipe(
              GEP, Plan->mapToVPValues(GEP->operands()), OrigLoop);
        } else if (CallInst *CI = dyn_cast<CallInst>(Inst)) {
          NewRecipe =
              new VPWidenCallRecipe(*CI, Plan->mapToVPValues(CI->args()),
                                    getVectorIntrinsicIDForCall(CI, &TLI));
        } else if (SelectInst *SI = dyn_cast<SelectInst>(Inst)) {
          bool InvariantCond =
              SE.isLoopInvariant(SE.getSCEV(SI->getOperand(0)), OrigLoop);
          NewRecipe = new VPWidenSelectRecipe(
              *SI, Plan->mapToVPValues(SI->operands()), InvariantCond);
        } else {
          NewRecipe =
              new VPWidenRecipe(*Inst, Plan->mapToVPValues(Inst->operands()));
        }
      }

      NewRecipe->insertBefore(&Ingredient);
      if (NewRecipe->getNumDefinedValues() == 1)
        VPV->replaceAllUsesWith(NewRecipe->getVPSingleValue());
      else
        assert(NewRecipe->getNumDefinedValues() == 0 &&
               "Only recpies with zero or one defined values expected");
      Ingredient.eraseFromParent();
      Plan->removeVPValueFor(Inst);
      for (auto *Def : NewRecipe->definedValues()) {
        Plan->addVPValue(Inst, Def);
      }
    }
  }
}

bool VPlanTransforms::sinkScalarOperands(VPlan &Plan) {
  auto Iter = depth_first(
      VPBlockRecursiveTraversalWrapper<VPBlockBase *>(Plan.getEntry()));
  bool Changed = false;
  // First, collect the operands of all predicated replicate recipes as seeds
  // for sinking.
  SetVector<std::pair<VPBasicBlock *, VPRecipeBase *>> WorkList;
  for (VPBasicBlock *VPBB : VPBlockUtils::blocksOnly<VPBasicBlock>(Iter)) {
    for (auto &Recipe : *VPBB) {
      auto *RepR = dyn_cast<VPReplicateRecipe>(&Recipe);
      if (!RepR || !RepR->isPredicated())
        continue;
      for (VPValue *Op : RepR->operands())
        if (auto *Def = Op->getDefiningRecipe())
          WorkList.insert(std::make_pair(VPBB, Def));
    }
  }

  bool ScalarVFOnly = Plan.hasScalarVFOnly();
  // Try to sink each replicate or scalar IV steps recipe in the worklist.
  while (!WorkList.empty()) {
    VPBasicBlock *SinkTo;
    VPRecipeBase *SinkCandidate;
    std::tie(SinkTo, SinkCandidate) = WorkList.pop_back_val();
    if (SinkCandidate->getParent() == SinkTo ||
        SinkCandidate->mayHaveSideEffects() ||
        SinkCandidate->mayReadOrWriteMemory())
      continue;
    if (auto *RepR = dyn_cast<VPReplicateRecipe>(SinkCandidate)) {
      if (!ScalarVFOnly && RepR->isUniform())
        continue;
    } else if (!isa<VPScalarIVStepsRecipe>(SinkCandidate))
      continue;

    bool NeedsDuplicating = false;
    // All recipe users of the sink candidate must be in the same block SinkTo
    // or all users outside of SinkTo must be uniform-after-vectorization (
    // i.e., only first lane is used) . In the latter case, we need to duplicate
    // SinkCandidate.
    auto CanSinkWithUser = [SinkTo, &NeedsDuplicating,
                            SinkCandidate](VPUser *U) {
      auto *UI = dyn_cast<VPRecipeBase>(U);
      if (!UI)
        return false;
      if (UI->getParent() == SinkTo)
        return true;
      NeedsDuplicating =
          UI->onlyFirstLaneUsed(SinkCandidate->getVPSingleValue());
      // We only know how to duplicate VPRecipeRecipes for now.
      return NeedsDuplicating && isa<VPReplicateRecipe>(SinkCandidate);
    };
    if (!all_of(SinkCandidate->getVPSingleValue()->users(), CanSinkWithUser))
      continue;

    if (NeedsDuplicating) {
      if (ScalarVFOnly)
        continue;
      Instruction *I = cast<Instruction>(
          cast<VPReplicateRecipe>(SinkCandidate)->getUnderlyingValue());
      auto *Clone =
          new VPReplicateRecipe(I, SinkCandidate->operands(), true, false);
      // TODO: add ".cloned" suffix to name of Clone's VPValue.

      Clone->insertBefore(SinkCandidate);
      SmallVector<VPUser *, 4> Users(
          SinkCandidate->getVPSingleValue()->users());
      for (auto *U : Users) {
        auto *UI = cast<VPRecipeBase>(U);
        if (UI->getParent() == SinkTo)
          continue;

        for (unsigned Idx = 0; Idx != UI->getNumOperands(); Idx++) {
          if (UI->getOperand(Idx) != SinkCandidate->getVPSingleValue())
            continue;
          UI->setOperand(Idx, Clone);
        }
      }
    }
    SinkCandidate->moveBefore(*SinkTo, SinkTo->getFirstNonPhi());
    for (VPValue *Op : SinkCandidate->operands())
      if (auto *Def = Op->getDefiningRecipe())
        WorkList.insert(std::make_pair(SinkTo, Def));
    Changed = true;
  }
  return Changed;
}

/// If \p R is a region with a VPBranchOnMaskRecipe in the entry block, return
/// the mask.
VPValue *getPredicatedMask(VPRegionBlock *R) {
  auto *EntryBB = dyn_cast<VPBasicBlock>(R->getEntry());
  if (!EntryBB || EntryBB->size() != 1 ||
      !isa<VPBranchOnMaskRecipe>(EntryBB->begin()))
    return nullptr;

  return cast<VPBranchOnMaskRecipe>(&*EntryBB->begin())->getOperand(0);
}

/// If \p R is a triangle region, return the 'then' block of the triangle.
static VPBasicBlock *getPredicatedThenBlock(VPRegionBlock *R) {
  auto *EntryBB = cast<VPBasicBlock>(R->getEntry());
  if (EntryBB->getNumSuccessors() != 2)
    return nullptr;

  auto *Succ0 = dyn_cast<VPBasicBlock>(EntryBB->getSuccessors()[0]);
  auto *Succ1 = dyn_cast<VPBasicBlock>(EntryBB->getSuccessors()[1]);
  if (!Succ0 || !Succ1)
    return nullptr;

  if (Succ0->getNumSuccessors() + Succ1->getNumSuccessors() != 1)
    return nullptr;
  if (Succ0->getSingleSuccessor() == Succ1)
    return Succ0;
  if (Succ1->getSingleSuccessor() == Succ0)
    return Succ1;
  return nullptr;
}

bool VPlanTransforms::mergeReplicateRegions(VPlan &Plan) {
  SetVector<VPRegionBlock *> DeletedRegions;
  bool Changed = false;

  // Collect region blocks to process up-front, to avoid iterator invalidation
  // issues while merging regions.
  SmallVector<VPRegionBlock *, 8> CandidateRegions(
      VPBlockUtils::blocksOnly<VPRegionBlock>(depth_first(
          VPBlockRecursiveTraversalWrapper<VPBlockBase *>(Plan.getEntry()))));

  // Check if Base is a predicated triangle, followed by an empty block,
  // followed by another predicate triangle. If that's the case, move the
  // recipes from the first to the second triangle.
  for (VPRegionBlock *Region1 : CandidateRegions) {
    if (DeletedRegions.contains(Region1))
      continue;
    auto *MiddleBasicBlock =
        dyn_cast_or_null<VPBasicBlock>(Region1->getSingleSuccessor());
    if (!MiddleBasicBlock || !MiddleBasicBlock->empty())
      continue;

    auto *Region2 =
        dyn_cast_or_null<VPRegionBlock>(MiddleBasicBlock->getSingleSuccessor());
    if (!Region2)
      continue;

    VPValue *Mask1 = getPredicatedMask(Region1);
    VPValue *Mask2 = getPredicatedMask(Region2);
    if (!Mask1 || Mask1 != Mask2)
      continue;
    VPBasicBlock *Then1 = getPredicatedThenBlock(Region1);
    VPBasicBlock *Then2 = getPredicatedThenBlock(Region2);
    if (!Then1 || !Then2)
      continue;

    assert(Mask1 && Mask2 && "both region must have conditions");

    // Note: No fusion-preventing memory dependencies are expected in either
    // region. Such dependencies should be rejected during earlier dependence
    // checks, which guarantee accesses can be re-ordered for vectorization.
    //
    // Move recipes to the successor region.
    for (VPRecipeBase &ToMove : make_early_inc_range(reverse(*Then1)))
      ToMove.moveBefore(*Then2, Then2->getFirstNonPhi());

    auto *Merge1 = cast<VPBasicBlock>(Then1->getSingleSuccessor());
    auto *Merge2 = cast<VPBasicBlock>(Then2->getSingleSuccessor());

    // Move VPPredInstPHIRecipes from the merge block to the successor region's
    // merge block. Update all users inside the successor region to use the
    // original values.
    for (VPRecipeBase &Phi1ToMove : make_early_inc_range(reverse(*Merge1))) {
      VPValue *PredInst1 =
          cast<VPPredInstPHIRecipe>(&Phi1ToMove)->getOperand(0);
      VPValue *Phi1ToMoveV = Phi1ToMove.getVPSingleValue();
      SmallVector<VPUser *> Users(Phi1ToMoveV->users());
      for (VPUser *U : Users) {
        auto *UI = dyn_cast<VPRecipeBase>(U);
        if (!UI || UI->getParent() != Then2)
          continue;
        for (unsigned I = 0, E = U->getNumOperands(); I != E; ++I) {
          if (Phi1ToMoveV != U->getOperand(I))
            continue;
          U->setOperand(I, PredInst1);
        }
      }

      Phi1ToMove.moveBefore(*Merge2, Merge2->begin());
    }

    // Finally, remove the first region.
    for (VPBlockBase *Pred : make_early_inc_range(Region1->getPredecessors())) {
      VPBlockUtils::disconnectBlocks(Pred, Region1);
      VPBlockUtils::connectBlocks(Pred, MiddleBasicBlock);
    }
    VPBlockUtils::disconnectBlocks(Region1, MiddleBasicBlock);
    DeletedRegions.insert(Region1);
  }

  for (VPRegionBlock *ToDelete : DeletedRegions)
    delete ToDelete;
  return Changed;
}

bool VPlanTransforms::mergeBlocksIntoPredecessors(VPlan &Plan) {
  SmallVector<VPBasicBlock *> WorkList;
  for (VPBasicBlock *VPBB : VPBlockUtils::blocksOnly<VPBasicBlock>(depth_first(
           VPBlockRecursiveTraversalWrapper<VPBlockBase *>(Plan.getEntry())))) {
    auto *PredVPBB =
        dyn_cast_or_null<VPBasicBlock>(VPBB->getSinglePredecessor());
    if (PredVPBB && PredVPBB->getNumSuccessors() == 1)
      WorkList.push_back(VPBB);
  }

  for (VPBasicBlock *VPBB : WorkList) {
    VPBasicBlock *PredVPBB = cast<VPBasicBlock>(VPBB->getSinglePredecessor());
    for (VPRecipeBase &R : make_early_inc_range(*VPBB))
      R.moveBefore(*PredVPBB, PredVPBB->end());
    VPBlockUtils::disconnectBlocks(PredVPBB, VPBB);
    auto *ParentRegion = cast_or_null<VPRegionBlock>(VPBB->getParent());
    if (ParentRegion && ParentRegion->getExiting() == VPBB)
      ParentRegion->setExiting(PredVPBB);
    SmallVector<VPBlockBase *> Successors(VPBB->successors());
    for (auto *Succ : Successors) {
      VPBlockUtils::disconnectBlocks(VPBB, Succ);
      VPBlockUtils::connectBlocks(PredVPBB, Succ);
    }
    delete VPBB;
  }
  return !WorkList.empty();
}

void VPlanTransforms::removeRedundantInductionCasts(VPlan &Plan) {
  for (auto &Phi : Plan.getVectorLoopRegion()->getEntryBasicBlock()->phis()) {
    auto *IV = dyn_cast<VPWidenIntOrFpInductionRecipe>(&Phi);
    if (!IV || IV->getTruncInst())
      continue;

    // A sequence of IR Casts has potentially been recorded for IV, which
    // *must be bypassed* when the IV is vectorized, because the vectorized IV
    // will produce the desired casted value. This sequence forms a def-use
    // chain and is provided in reverse order, ending with the cast that uses
    // the IV phi. Search for the recipe of the last cast in the chain and
    // replace it with the original IV. Note that only the final cast is
    // expected to have users outside the cast-chain and the dead casts left
    // over will be cleaned up later.
    auto &Casts = IV->getInductionDescriptor().getCastInsts();
    VPValue *FindMyCast = IV;
    for (Instruction *IRCast : reverse(Casts)) {
      VPRecipeBase *FoundUserCast = nullptr;
      for (auto *U : FindMyCast->users()) {
        auto *UserCast = cast<VPRecipeBase>(U);
        if (UserCast->getNumDefinedValues() == 1 &&
            UserCast->getVPSingleValue()->getUnderlyingValue() == IRCast) {
          FoundUserCast = UserCast;
          break;
        }
      }
      FindMyCast = FoundUserCast->getVPSingleValue();
    }
    FindMyCast->replaceAllUsesWith(IV);
  }
}

void VPlanTransforms::removeRedundantCanonicalIVs(VPlan &Plan) {
  VPCanonicalIVPHIRecipe *CanonicalIV = Plan.getCanonicalIV();
  VPWidenCanonicalIVRecipe *WidenNewIV = nullptr;
  for (VPUser *U : CanonicalIV->users()) {
    WidenNewIV = dyn_cast<VPWidenCanonicalIVRecipe>(U);
    if (WidenNewIV)
      break;
  }

  if (!WidenNewIV)
    return;

  VPBasicBlock *HeaderVPBB = Plan.getVectorLoopRegion()->getEntryBasicBlock();
  for (VPRecipeBase &Phi : HeaderVPBB->phis()) {
    auto *WidenOriginalIV = dyn_cast<VPWidenIntOrFpInductionRecipe>(&Phi);

    if (!WidenOriginalIV || !WidenOriginalIV->isCanonical() ||
        WidenOriginalIV->getScalarType() != WidenNewIV->getScalarType())
      continue;

    // Replace WidenNewIV with WidenOriginalIV if WidenOriginalIV provides
    // everything WidenNewIV's users need. That is, WidenOriginalIV will
    // generate a vector phi or all users of WidenNewIV demand the first lane
    // only.
    if (WidenOriginalIV->needsVectorIV() ||
        vputils::onlyFirstLaneUsed(WidenNewIV)) {
      WidenNewIV->replaceAllUsesWith(WidenOriginalIV);
      WidenNewIV->eraseFromParent();
      return;
    }
  }
}

void VPlanTransforms::removeDeadRecipes(VPlan &Plan) {
  ReversePostOrderTraversal<VPBlockRecursiveTraversalWrapper<VPBlockBase *>>
      RPOT(Plan.getEntry());

  for (VPBasicBlock *VPBB : reverse(VPBlockUtils::blocksOnly<VPBasicBlock>(RPOT))) {
    // The recipes in the block are processed in reverse order, to catch chains
    // of dead recipes.
    for (VPRecipeBase &R : make_early_inc_range(reverse(*VPBB))) {
      if (R.mayHaveSideEffects() || any_of(R.definedValues(), [](VPValue *V) {
            return V->getNumUsers() > 0;
          }))
        continue;
      R.eraseFromParent();
    }
  }
}

void VPlanTransforms::optimizeInductions(VPlan &Plan, ScalarEvolution &SE) {
  SmallVector<VPRecipeBase *> ToRemove;
  VPBasicBlock *HeaderVPBB = Plan.getVectorLoopRegion()->getEntryBasicBlock();
  bool HasOnlyVectorVFs = !Plan.hasVF(ElementCount::getFixed(1));
  for (VPRecipeBase &Phi : HeaderVPBB->phis()) {
    auto *WideIV = dyn_cast<VPWidenIntOrFpInductionRecipe>(&Phi);
    if (!WideIV)
      continue;
    if (HasOnlyVectorVFs && none_of(WideIV->users(), [WideIV](VPUser *U) {
          return U->usesScalars(WideIV);
        }))
      continue;

    auto IP = HeaderVPBB->getFirstNonPhi();
    VPCanonicalIVPHIRecipe *CanonicalIV = Plan.getCanonicalIV();
    Type *ResultTy = WideIV->getPHINode()->getType();
    if (Instruction *TruncI = WideIV->getTruncInst())
      ResultTy = TruncI->getType();
    const InductionDescriptor &ID = WideIV->getInductionDescriptor();
    VPValue *Step =
        vputils::getOrCreateVPValueForSCEVExpr(Plan, ID.getStep(), SE);
    VPValue *BaseIV = CanonicalIV;
    if (!CanonicalIV->isCanonical(ID, ResultTy)) {
      BaseIV = new VPDerivedIVRecipe(ID, WideIV->getStartValue(), CanonicalIV,
                                     Step, ResultTy);
      HeaderVPBB->insert(BaseIV->getDefiningRecipe(), IP);
    }

    VPScalarIVStepsRecipe *Steps = new VPScalarIVStepsRecipe(ID, BaseIV, Step);
    HeaderVPBB->insert(Steps, IP);

    // Update scalar users of IV to use Step instead. Use SetVector to ensure
    // the list of users doesn't contain duplicates.
    SetVector<VPUser *> Users(WideIV->user_begin(), WideIV->user_end());
    for (VPUser *U : Users) {
      if (HasOnlyVectorVFs && !U->usesScalars(WideIV))
        continue;
      for (unsigned I = 0, E = U->getNumOperands(); I != E; I++) {
        if (U->getOperand(I) != WideIV)
          continue;
        U->setOperand(I, Steps);
      }
    }
  }
}

void VPlanTransforms::removeRedundantExpandSCEVRecipes(VPlan &Plan) {
  DenseMap<const SCEV *, VPValue *> SCEV2VPV;

  for (VPRecipeBase &R :
       make_early_inc_range(*Plan.getEntry()->getEntryBasicBlock())) {
    auto *ExpR = dyn_cast<VPExpandSCEVRecipe>(&R);
    if (!ExpR)
      continue;

    auto I = SCEV2VPV.insert({ExpR->getSCEV(), ExpR});
    if (I.second)
      continue;
    ExpR->replaceAllUsesWith(I.first->second);
    ExpR->eraseFromParent();
  }
}

static bool canSimplifyBranchOnCond(VPInstruction *Term) {
  VPInstruction *Not = dyn_cast<VPInstruction>(Term->getOperand(0));
  if (!Not || Not->getOpcode() != VPInstruction::Not)
    return false;

  VPInstruction *ALM = dyn_cast<VPInstruction>(Not->getOperand(0));
  return ALM && ALM->getOpcode() == VPInstruction::ActiveLaneMask;
}

void VPlanTransforms::optimizeForVFAndUF(VPlan &Plan, ElementCount BestVF,
                                         unsigned BestUF,
                                         PredicatedScalarEvolution &PSE) {
  assert(Plan.hasVF(BestVF) && "BestVF is not available in Plan");
  assert(Plan.hasUF(BestUF) && "BestUF is not available in Plan");
  VPBasicBlock *ExitingVPBB =
      Plan.getVectorLoopRegion()->getExitingBasicBlock();
  auto *Term = dyn_cast<VPInstruction>(&ExitingVPBB->back());
  // Try to simplify the branch condition if TC <= VF * UF when preparing to
  // execute the plan for the main vector loop. We only do this if the
  // terminator is:
  //  1. BranchOnCount, or
  //  2. BranchOnCond where the input is Not(ActiveLaneMask).
  if (!Term || (Term->getOpcode() != VPInstruction::BranchOnCount &&
                (Term->getOpcode() != VPInstruction::BranchOnCond ||
                 !canSimplifyBranchOnCond(Term))))
    return;

  Type *IdxTy =
      Plan.getCanonicalIV()->getStartValue()->getLiveInIRValue()->getType();
  const SCEV *TripCount = createTripCountSCEV(IdxTy, PSE);
  ScalarEvolution &SE = *PSE.getSE();
  const SCEV *C =
      SE.getConstant(TripCount->getType(), BestVF.getKnownMinValue() * BestUF);
  if (TripCount->isZero() ||
      !SE.isKnownPredicate(CmpInst::ICMP_ULE, TripCount, C))
    return;

  LLVMContext &Ctx = SE.getContext();
  auto *BOC =
      new VPInstruction(VPInstruction::BranchOnCond,
                        {Plan.getOrAddExternalDef(ConstantInt::getTrue(Ctx))});
  Term->eraseFromParent();
  ExitingVPBB->appendRecipe(BOC);
  Plan.setVF(BestVF);
  Plan.setUF(BestUF);
  // TODO: Further simplifications are possible
  //      1. Replace inductions with constants.
  //      2. Replace vector loop region with VPBasicBlock.
}
