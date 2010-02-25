/*
   For more information, please see: http://software.sci.utah.edu

   The MIT License

   Copyright (c) 2008 Scientific Computing and Imaging Institute,
   University of Utah.


   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
*/

/**
  \file    AbstrRenderer.cpp
  \author  Jens Krueger
           SCI Institute
           University of Utah
  \date    August 2008
*/

#include <algorithm>
#include "AbstrRenderer.h"
#include <Controller/Controller.h>
#include <IO/Tuvok_QtPlugins.h>
#include <IO/IOManager.h>
#include <Renderer/GPUMemMan/GPUMemMan.h>
#include "Basics/MathTools.h"
#include "Basics/GeometryGenerator.h"
#include "IO/ExternalDataset.h"

using namespace std;
using namespace tuvok;

AbstrRenderer::AbstrRenderer(MasterController* pMasterController,
                             bool bUseOnlyPowerOfTwo, bool bDownSampleTo8Bits,
                             bool bDisableBorder, enum ScalingMethod sm) :
  m_pMasterController(pMasterController),
  m_eRenderMode(RM_1DTRANS),
  m_eBlendPrecision(BP_32BIT),
  m_bUseLighting(true),
  m_pDataset(NULL),
  m_p1DTrans(NULL),
  m_p2DTrans(NULL),
  m_fSampleRateModifier(1.0f),
  m_vIsoColor(0.5,0.5,0.5),
  m_vTextColor(1,1,1,1),
  m_bRenderGlobalBBox(false),
  m_bRenderLocalBBox(false),
  m_vWinSize(0,0),
  m_iLogoPos(3),
  m_strLogoFilename(""),
  m_iLODNotOKCounter(0),
  m_fMaxMSPerFrame(10000),
  m_fScreenResDecFactor(2.0f),
  m_fSampleDecFactor(2.0f),
  m_bUseAllMeans(false),
  m_bOffscreenIsLowRes(false),
  m_iStartDelay(1000),
  m_iMinLODForCurrentView(0),
  m_iTimeSliceMSecs(100),
  m_iIntraFrameCounter(0),
  m_iFrameCounter(0),
  m_iCheckCounter(0),
  m_iMaxLODIndex(0),
  m_iLODLimits(0,0),
  m_iPerformanceBasedLODSkip(0),
  m_iCurrentLODOffset(0),
  m_iStartLODOffset(0),
  m_iTimestep(0),
  m_bClearFramebuffer(true),
  m_bConsiderPreviousDepthbuffer(true),
  m_iCurrentLOD(0),
  m_iBricksRenderedInThisSubFrame(0),
  m_bCaptureMode(false),
  m_bMIPLOD(true),
  m_fMIPRotationAngle(0.0f),
  m_bOrthoView(false),
  m_bRenderCoordArrows(false),
  m_bRenderPlanesIn3D(false),
  m_bDoClearView(false),
  m_vCVColor(1,0,0),
  m_fCVSize(5.5f),
  m_fCVContextScale(1.0f),
  m_fCVBorderScale(60.0f),
  m_vCVMousePos(200, 200),
  m_vCVPos(0,0,0,0),
  m_bPerformReCompose(false),
  m_bRequestStereoRendering(false),
  m_bDoStereoRendering(false),
  m_fStereoEyeDist(0.02f),
  m_fStereoFocalLength(1.0f),
  m_bUseOnlyPowerOfTwo(bUseOnlyPowerOfTwo),
  m_bDownSampleTo8Bits(bDownSampleTo8Bits),
  m_bDisableBorder(bDisableBorder),
  m_bAvoidSeperateCompositing(true),
  m_TFScalingMethod(sm),
  m_bClipPlaneOn(false),
  m_bClipPlaneDisplayed(true),
  m_bClipPlaneLocked(true),
  m_vEye(0,0,1.6f),
  m_vAt(0,0,0),
  m_vUp(0,1,0),
  m_fFOV(50.0f),
  m_fZNear(0.1f),
  m_fZFar(100.0f),
  m_cAmbient(1.0f,1.0f,1.0f,0.2f),
  m_cDiffuse(1.0f,1.0f,1.0f,0.8f),
  m_cSpecular(1.0f,1.0f,1.0f,1.0f),
  m_fIsovalue(0.5f),
  m_fCVIsovalue(0.8f)
{
  m_vBackgroundColors[0] = FLOATVECTOR3(0,0,0);
  m_vBackgroundColors[1] = FLOATVECTOR3(0,0,0);

  simpleRenderRegion3D.minCoord = UINTVECTOR2(0,0); // maxCoord is updated in Paint().
  renderRegions.push_back(&simpleRenderRegion3D);

  for (size_t i=0; i < renderRegions.size(); ++i)
    RestartTimers(*renderRegions[i]);

  m_vShaderSearchDirs.push_back("Shaders");
  m_vShaderSearchDirs.push_back("Tuvok/Shaders");
  m_vShaderSearchDirs.push_back("../Tuvok/Shaders");
  m_vArrowGeometry = GeometryGenerator::GenArrow(0.3f,0.8f,0.006f,0.012f,20);
}

bool AbstrRenderer::Initialize() {
  return m_pDataset != NULL;
}

bool AbstrRenderer::LoadDataset(const string& strFilename, bool& bRebrickingRequired) {
  if (m_pMasterController == NULL) return false;

  if (m_pMasterController->IOMan() == NULL) {
    T_ERROR("Cannot load dataset because IOManager is NULL");
    return false;
  }

  m_pDataset = m_pMasterController->IOMan()->LoadDataset(strFilename,this, bRebrickingRequired);

  if (m_pDataset == NULL) {
    T_ERROR("IOManager call to load dataset failed.");
    return false;
  }

  MESSAGE("Load successful, initializing renderer!");

  Controller::Instance().Provenance("file", "open", strFilename);

  // find the maximum LOD index
  m_iMaxLODIndex = m_pDataset->GetLODLevelCount()-1;

  // now that we know the range of the dataset, we can set the default
  // isoval to half the range.  For CV, we'll set the isovals to a bit above
  // the context isoval; with any luck, it'll make a valid image right off the
  // bat.
  std::pair<double,double> rng = m_pDataset->GetRange();
  // It can happen that we don't know the range; old UVFs, for example.  We'll
  // know this because the minimum will be g.t. the maximum.
  if(rng.first > rng.second) {
    m_fIsovalue = rng.second / 2.0f;
  } else {
    m_fIsovalue = (rng.second-rng.first) / 2.0f;
  }
  m_fCVIsovalue = m_fIsovalue + (m_fIsovalue/2.0f);

  return true;
}

AbstrRenderer::~AbstrRenderer() {
  if (m_pDataset) m_pMasterController->MemMan()->FreeDataset(m_pDataset, this);
  if (m_p1DTrans) m_pMasterController->MemMan()->Free1DTrans(m_p1DTrans, this);
  if (m_p2DTrans) m_pMasterController->MemMan()->Free2DTrans(m_p2DTrans, this);
}

static std::string render_mode(AbstrRenderer::ERenderMode mode) {
  switch(mode) {
    case AbstrRenderer::RM_1DTRANS: return "mode1d";
    case AbstrRenderer::RM_2DTRANS: return "mode2d";
    case AbstrRenderer::RM_ISOSURFACE: return "modeiso";
    default: return "invalid";
  };
}

void AbstrRenderer::SetRendermode(ERenderMode eRenderMode)
{
  if (m_eRenderMode != eRenderMode) {
    m_eRenderMode = eRenderMode;
    ScheduleCompleteRedraw();
    Controller::Instance().Provenance("mode", render_mode(eRenderMode));
  }
}

void AbstrRenderer::SetUseLighting(bool bUseLighting) {
  if (m_bUseLighting != bUseLighting) {
    m_bUseLighting = bUseLighting;
    Schedule3DWindowRedraws();
    Controller::Instance().Provenance("light", "lighting");
  }
}

void AbstrRenderer::SetBlendPrecision(EBlendPrecision eBlendPrecision) {
  if (m_eBlendPrecision != eBlendPrecision) {
    m_eBlendPrecision = eBlendPrecision;
    ScheduleCompleteRedraw();
  }
}

void AbstrRenderer::SetDataset(Dataset *vds)
{
  if(m_pDataset) {
    Controller::Instance().MemMan()->FreeDataset(vds, this);
    delete m_pDataset;
  }
  m_pDataset = vds;
  Controller::Instance().MemMan()->AddDataset(m_pDataset, this);
  ScheduleCompleteRedraw();
  Controller::Instance().Provenance("file", "open", "<in_memory_buffer>");
}

/*
void AbstrRenderer::UpdateData(const BrickKey& bk,
                               std::tr1::shared_ptr<float> fp, size_t len)
{
  MESSAGE("Updating data with %u element array", static_cast<UINT32>(len));
  // free old data; we know we'll never need it, at this point.
  Controller::Instance().MemMan()->FreeAssociatedTextures(m_pDataset);
  dynamic_cast<ExternalDataset*>(m_pDataset)->UpdateData(bk, fp, len);
}
*/

void AbstrRenderer::Free1DTrans()
{
  GPUMemMan& mm = *(Controller::Instance().MemMan());
  mm.Free1DTrans(m_p1DTrans, this);
}

void AbstrRenderer::Changed1DTrans() {
  AbstrDebugOut *dbg = m_pMasterController->DebugOut();
  if (m_eRenderMode != RM_1DTRANS) {
    dbg->Message(_func_,
                 "not using the 1D transferfunction at the moment, "
                 "ignoring message");
  } else {
    dbg->Message(_func_, "complete redraw scheduled");
    ScheduleCompleteRedraw();
    // No provenance; as a mechanism to filter out too many updates, we place
    // the onus on updating this in the UI which is driving us.
  }
}

void AbstrRenderer::Changed2DTrans() {
  AbstrDebugOut *dbg = m_pMasterController->DebugOut();
  if (m_eRenderMode != RM_2DTRANS) {
    dbg->Message(_func_,
                 "not using the 2D transferfunction at the moment, "
                 "ignoring message");
  } else {
    dbg->Message(_func_, "complete redraw scheduled");
    ScheduleCompleteRedraw();
    // No provenance; handled by application, not Tuvok lib.
  }
}


void AbstrRenderer::SetSampleRateModifier(float fSampleRateModifier) {
  if(m_fSampleRateModifier != fSampleRateModifier) {
    m_fSampleRateModifier = fSampleRateModifier;
    Schedule3DWindowRedraws();
  }
}

void AbstrRenderer::SetIsoValue(float fIsovalue) {
  if(fIsovalue != m_fIsovalue) {
    m_fIsovalue = fIsovalue;
    Schedule3DWindowRedraws();
  }
}

double AbstrRenderer::GetNormalizedIsovalue() const
{
  if(m_pDataset->GetBitWidth() != 8 && m_bDownSampleTo8Bits) {
    double mx;
    if(m_pDataset->GetRange().first > m_pDataset->GetRange().second) {
      mx = m_p1DTrans->GetSize();
    } else {
      mx = m_pDataset->GetRange().second;
    }
    return MathTools::lerp(m_fIsovalue, 0.f,static_cast<float>(mx), 0.f,1.f);
  }
  return m_fIsovalue / (1 << m_pDataset->GetBitWidth());
}

double AbstrRenderer::GetNormalizedCVIsovalue() const
{
  if(m_pDataset->GetBitWidth() != 8 && m_bDownSampleTo8Bits) {
    double mx;
    if(m_pDataset->GetRange().first > m_pDataset->GetRange().second) {
      mx = m_p1DTrans->GetSize();
    } else {
      mx = m_pDataset->GetRange().second;
    }
    return MathTools::lerp(m_fCVIsovalue, 0.f,static_cast<float>(mx), 0.f,1.f);
  }
  return m_fCVIsovalue / (1 << m_pDataset->GetBitWidth());
}

bool AbstrRenderer::CheckForRedraw() {
  if (m_vWinSize.area() == 0)
    return false; // can't draw to a size zero window.

  bool decrementCounter = false;
  bool redrawRequired = false;
  redrawRequired = m_bPerformReCompose;

  for (size_t i=0; i < renderRegions.size(); ++i) {
    const RenderRegion* region = renderRegions[i];
    // need to redraw for 1 of three reasons:
    //   didn't finish last paint call; bricks remain.
    //   haven't rendered the finest LOD for the current view
    //   last draw was low res or sample rate for interactivity
    if (m_vCurrentBrickList.size() > m_iBricksRenderedInThisSubFrame ||
        m_iCurrentLODOffset > m_iMinLODForCurrentView ||
        region->doAnotherRedrawDueToAllMeans) {
      if (m_iCheckCounter == 0 || m_bCaptureMode) {
        MESSAGE("Still drawing...");
        return true;
      } else {
        decrementCounter = true;
      }
    }
    // region is completely blank?
    redrawRequired |= region->isBlank;
  }
  /// @todo Is this logic for how/when to decrement correct?
  if (decrementCounter)
    m_iCheckCounter--;

  return redrawRequired;
}

void AbstrRenderer::Resize(const UINTVECTOR2& vWinSize) {
  m_vWinSize = vWinSize;
  ScheduleCompleteRedraw();
}

RenderRegion3D* AbstrRenderer::GetFirst3DRegion() {
  for (size_t i=0; i < renderRegions.size(); ++i) {
    if (renderRegions[i]->is3D())
      return dynamic_cast<RenderRegion3D*>(renderRegions[i]);
  }
  return NULL;
}

void AbstrRenderer::SetRotation(RenderRegion *renderRegion,
                                const FLOATMATRIX4& rotation) {
  renderRegion->rotation = rotation;
  ScheduleWindowRedraw(renderRegion);
}

const FLOATMATRIX4&
AbstrRenderer::GetRotation(const RenderRegion* renderRegion) const {
  return renderRegion->rotation;
}

void AbstrRenderer::SetTranslation(RenderRegion *renderRegion,
                                   const FLOATMATRIX4& mTranslation) {
  renderRegion->translation = mTranslation;
  ScheduleWindowRedraw(renderRegion);
}

const FLOATMATRIX4&
AbstrRenderer::GetTranslation(const RenderRegion* renderRegion) const {
  return renderRegion->translation;
}

void AbstrRenderer::SetClipPlane(RenderRegion *renderRegion,
                                 const ExtendedPlane& plane)
{
  if(plane == m_ClipPlane) { return; }
  m_ClipPlane = plane; /// @todo: Make this per RenderRegion.
  ScheduleWindowRedraw(renderRegion);
}


void AbstrRenderer::EnableClipPlane(RenderRegion *renderRegion) {
  if (!renderRegion)
    renderRegion = GetFirst3DRegion();
  if (renderRegion) {
    if(!m_bClipPlaneOn) {
      m_bClipPlaneOn = true; /// @todo: Make this per RenderRegion.
      ScheduleWindowRedraw(renderRegion);
      Controller::Instance().Provenance("clip", "clip", "enable");
    }
  }
}
void AbstrRenderer::DisableClipPlane(RenderRegion *renderRegion) {
  if (!renderRegion)
    renderRegion = GetFirst3DRegion();
  if (renderRegion) {
    if(m_bClipPlaneOn) {
      m_bClipPlaneOn = false; /// @todo: Make this per RenderRegion.
      ScheduleWindowRedraw(renderRegion);
      Controller::Instance().Provenance("clip", "clip", "disable");
    }
  }
}
void AbstrRenderer::ShowClipPlane(bool bShown,
                                  RenderRegion *renderRegion) {
  if (!renderRegion)
    renderRegion = GetFirst3DRegion();
  if (renderRegion) {
    m_bClipPlaneDisplayed = bShown; /// @todo: Make this per RenderRegion.
    if(m_bClipPlaneOn) {
      ScheduleWindowRedraw(renderRegion);
      Controller::Instance().Provenance("clip", "showclip", "enable");
    }
  }
}
void AbstrRenderer::ClipPlaneRelativeLock(bool bRel) {
  m_bClipPlaneLocked = bRel;/// @todo: Make this per RenderRegion ?
}

void AbstrRenderer::SetSliceDepth(RenderRegion *renderRegion, UINT64 sliceDepth) {
  if (renderRegion->GetSliceIndex() != sliceDepth) {
    renderRegion->SetSliceIndex(sliceDepth);
    ScheduleWindowRedraw(renderRegion);
    if (m_bRenderPlanesIn3D)
      Schedule3DWindowRedraws();
  }
}

UINT64 AbstrRenderer::GetSliceDepth(const RenderRegion *renderRegion) const {
  return renderRegion->GetSliceIndex();
}

void AbstrRenderer::SetGlobalBBox(bool bRenderBBox) {
  m_bRenderGlobalBBox = bRenderBBox; /// @todo: Make this per RenderRegion.
  Schedule3DWindowRedraws();
  Controller::Instance().Provenance("boundingbox", "global_bbox");
}

void AbstrRenderer::SetLocalBBox(bool bRenderBBox) {
  m_bRenderLocalBBox = bRenderBBox; /// @todo: Make this per RenderRegion.
  Schedule3DWindowRedraws();
  Controller::Instance().Provenance("boundingbox", "local_bbox");
}

void AbstrRenderer::ScheduleCompleteRedraw() {
  m_iCheckCounter = m_iStartDelay;

  for (size_t i=0; i < renderRegions.size(); ++i) {
    renderRegions[i]->redrawMask = true;
    renderRegions[i]->isBlank = true;
    renderRegions[i]->isTargetBlank = true;
  }
}

void AbstrRenderer::Schedule3DWindowRedraws() {
  m_iCheckCounter = m_iStartDelay;

  for (size_t i=0; i < renderRegions.size(); ++i) {
    if (renderRegions[i]->is3D()) {
      renderRegions[i]->redrawMask = true;
      renderRegions[i]->isBlank = true;
      renderRegions[i]->isTargetBlank = true;
    }
  }
}

void AbstrRenderer::ScheduleWindowRedraw(RenderRegion *renderRegion) {
  m_iCheckCounter = m_iStartDelay;
  renderRegion->redrawMask = true;
  renderRegion->isBlank = true;
  renderRegion->isTargetBlank = true;
}

void AbstrRenderer::ScheduleRecompose(RenderRegion *renderRegion) {
  if (!renderRegion)
    renderRegion = GetFirst3DRegion();
  if (renderRegion) {
    if(!m_bAvoidSeperateCompositing && // ensure we finished the current frame:
       m_vCurrentBrickList.size() == m_iBricksRenderedInThisSubFrame) {
      m_bPerformReCompose = true;
      renderRegion->redrawMask = true;
    } else {
      ScheduleWindowRedraw(renderRegion);
    }
  }
}


void AbstrRenderer::CompletedASubframe(RenderRegion* region) {
  bool bRenderingFirstSubFrame =
    (m_iCurrentLODOffset == m_iStartLODOffset) &&
    (!region->decreaseScreenRes || region->decreaseScreenResNow) &&
    (!region->decreaseSamplingRate || region->decreaseSamplingRateNow);
  bool bSecondSubFrame =
    !bRenderingFirstSubFrame &&
    (m_iCurrentLODOffset == m_iStartLODOffset ||
     (m_iCurrentLODOffset == m_iStartLODOffset-1 &&
      !(region->decreaseScreenRes || region->decreaseSamplingRate)));

  if (bRenderingFirstSubFrame) {
    // time for current interaction LOD -> to detect if we are to slow
    region->msecPassed[0] = region->msecPassedCurrentFrame;
  } else if(bSecondSubFrame) {
    region->msecPassed[1] = region->msecPassedCurrentFrame;
  }
  region->msecPassedCurrentFrame = 0.0f;
  region->isTargetBlank = false;
}

void AbstrRenderer::RestartTimer(RenderRegion& region, const size_t iTimerIndex) {
  region.msecPassed[iTimerIndex] = -1.0f;
}

void AbstrRenderer::RestartTimers(RenderRegion& region) {
  RestartTimer(region, 0);
  RestartTimer(region, 1);
}

void AbstrRenderer::ComputeMaxLODForCurrentView(RenderRegion& region) {
  if (!m_bCaptureMode && region.msecPassed[0]>=0.0f) {
    // if rendering is too slow use a lower resolution during interaction
    if (region.msecPassed[0] > m_fMaxMSPerFrame) {
      // wait for 3 frames before switching to lower lod (3 here is
      // chosen more or less arbitrary, can be changed if needed)
      if (m_iLODNotOKCounter < 3) {
        MESSAGE("Would increase start LOD but will give the renderer %u "
                "more frame(s) time to become faster", 3 - m_iLODNotOKCounter);
        m_iLODNotOKCounter++;
      } else {
        // We gave it a chance but rendering was too slow. So let's drop down
        // in quality.
        m_iLODNotOKCounter = 0;

        // Easiest thing is to try rendering a lower quality LOD. So try this
        // if possible.
        UINT64 iPerformanceBasedLODSkip =
          std::max<UINT64>(1, m_iPerformanceBasedLODSkip) - 1;
        if (m_iPerformanceBasedLODSkip != iPerformanceBasedLODSkip) {
          MESSAGE("Increasing start LOD to %llu as it took %g ms "
                  "to render the first LOD level (max is %g) ",
                  m_iPerformanceBasedLODSkip, region.msecPassed[0],
                  m_fMaxMSPerFrame);
          region.msecPassed[0] = region.msecPassed[1];
          m_iPerformanceBasedLODSkip = iPerformanceBasedLODSkip;
        } else {
          // Already at lowest quality LOD, so will need to try something else.
          MESSAGE("Would like to increase start LOD as it took %g ms "
                  "to render the first LOD level (max is %g) BUT CAN'T.",
                  region.msecPassed[0], m_fMaxMSPerFrame);
          if (m_bUseAllMeans) {
            if (region.decreaseSamplingRate && region.decreaseScreenRes) {
              MESSAGE("Even with UseAllMeans there is nothing that "
                      "can be done to meet the specified framerate.");
            } else {
              if (!region.decreaseScreenRes) {
                MESSAGE("UseAllMeans enabled: decreasing resolution "
                        "to meet target framerate");
                region.decreaseScreenRes = true;
              } else {
                MESSAGE("UseAllMeans enabled: decreasing sampling rate "
                        "to meet target framerate");
                region.decreaseSamplingRate = true;
              }
            }
          } else {
            MESSAGE("UseAllMeans disabled so framerate can not be met...");
          }
        }
      }
    } else {
      // if finished rendering last frame (m_iBricksRenderedInThisSubFrame is
      // from the last frame, not the new one we are about to start) and did
      // this fast enough, use a higher resolution during interaction.
      if (m_vCurrentBrickList.size() == m_iBricksRenderedInThisSubFrame &&
          region.msecPassed[1] >= 0.0f &&
          region.msecPassed[1] <= m_fMaxMSPerFrame) {
        m_iLODNotOKCounter = 0;
        // We're rendering fast, so lets step up the quality. Easiest thing to
        // try first is rendering at normal sampling rate and resolution.
        if (region.decreaseSamplingRate || region.decreaseScreenRes) {
          if (region.decreaseSamplingRate) {
            MESSAGE("Rendering at full resolution as this took only %g ms",
                    region.msecPassed[0]);
            region.decreaseSamplingRate = false;
          } else {
            if (region.decreaseScreenRes) {
              MESSAGE("Rendering to full viewport as this took only %g ms",
                      region.msecPassed[0]);
              region.decreaseScreenRes = false;
            }
          }
        } else {
          // Let's try rendering at a higher quality LOD.
          UINT64 iPerformanceBasedLODSkip =
            std::min<UINT64>(m_iMaxLODIndex - m_iMinLODForCurrentView,
                             m_iPerformanceBasedLODSkip + 1);
          if (m_iPerformanceBasedLODSkip != iPerformanceBasedLODSkip) {
            MESSAGE("Decreasing start LOD to %llu as it took only %g ms "
                    "to render the second LOD level",
                    m_iPerformanceBasedLODSkip, region.msecPassed[1]);
            m_iPerformanceBasedLODSkip = iPerformanceBasedLODSkip;
          }
        }
      } else {
        if (m_vCurrentBrickList.size() == m_iBricksRenderedInThisSubFrame) {
          MESSAGE("Start LOD seems to be ok");
        }
      }
    }

    m_iStartLODOffset = std::max(m_iMinLODForCurrentView,
                                 m_iMaxLODIndex - m_iPerformanceBasedLODSkip);
  } else if (m_bCaptureMode){
    m_iStartLODOffset = m_iMinLODForCurrentView;
  } else {
    // This is our very first render, let's take it easy.
    m_iStartLODOffset = m_iMaxLODIndex;
  }

  m_iStartLODOffset = std::min(m_iStartLODOffset,
                               static_cast<UINT64>(m_iMaxLODIndex -
                                                   m_iLODLimits.x));
  m_iCurrentLODOffset = m_iStartLODOffset;
  RestartTimers(region);
}

void AbstrRenderer::ComputeMinLODForCurrentView() {
  // compute scale factor for domain
  UINTVECTOR3 vDomainSize = UINTVECTOR3(m_pDataset->GetDomainSize());
  FLOATVECTOR3 vScale = FLOATVECTOR3(m_pDataset->GetScale());
  FLOATVECTOR3 vExtend = FLOATVECTOR3(vDomainSize) * vScale;
  vExtend /= vExtend.maxVal();

  /// @todo consider real extent not center
  FLOATVECTOR3 vfCenter(0,0,0);
  m_iMinLODForCurrentView = static_cast<UINT64>(
    MathTools::Clamp(m_FrustumCullingLOD.GetLODLevel(vfCenter, vExtend,
                                                     vDomainSize),
                     static_cast<int>(m_iLODLimits.y),
                     static_cast<int>(m_pDataset->GetLODLevelCount()-1)));
}

/// Calculates the distance to a given brick given the current view
/// transformation.  There is a slight offset towards the center, which helps
/// avoid ambiguous cases.
static float
brick_distance(const Brick &b, const FLOATMATRIX4 &mat_modelview)
{
  const float fEpsilon = 0.4999f;
  const FLOATVECTOR3 vEpsilonEdges[8] = {
    b.vCenter + FLOATVECTOR3(-b.vExtension.x,-b.vExtension.y,-b.vExtension.z) *
      fEpsilon,
    b.vCenter + FLOATVECTOR3(-b.vExtension.x,-b.vExtension.y,+b.vExtension.z) *
      fEpsilon,
    b.vCenter + FLOATVECTOR3(-b.vExtension.x,+b.vExtension.y,-b.vExtension.z) *
      fEpsilon,
    b.vCenter + FLOATVECTOR3(-b.vExtension.x,+b.vExtension.y,+b.vExtension.z) *
      fEpsilon,
    b.vCenter + FLOATVECTOR3(+b.vExtension.x,-b.vExtension.y,-b.vExtension.z) *
      fEpsilon,
    b.vCenter + FLOATVECTOR3(+b.vExtension.x,-b.vExtension.y,+b.vExtension.z) *
      fEpsilon,
    b.vCenter + FLOATVECTOR3(+b.vExtension.x,+b.vExtension.y,-b.vExtension.z) *
      fEpsilon,
    b.vCenter + FLOATVECTOR3(+b.vExtension.x,+b.vExtension.y,+b.vExtension.z) *
      fEpsilon
  };

  // final distance is the distance to the closest corner.
  float fDistance = std::numeric_limits<float>::max();
  for(size_t i=0; i < 8; ++i) {
    fDistance = std::min(
                  fDistance,
                  (FLOATVECTOR4(vEpsilonEdges[i],1.0f)*mat_modelview)
                    .xyz().length()
                );
  }
  return fDistance;
}

vector<Brick> AbstrRenderer::BuildLeftEyeSubFrameBrickList(
                             RenderRegion& renderRegion,
                             const vector<Brick>& vRightEyeBrickList) {
  vector<Brick> vBrickList = vRightEyeBrickList;

  for (UINT32 iBrick = 0;iBrick<vBrickList.size();iBrick++) {
    // compute minimum distance to brick corners (offset slightly to
    // the center to resolve ambiguities).
    vBrickList[iBrick].fDistance = brick_distance(vBrickList[iBrick],
                                                  renderRegion.modelView[1]);
  }

  sort(vBrickList.begin(), vBrickList.end());

  return vBrickList;
}

double AbstrRenderer::MaxValue() const {
  if (m_pDataset->GetBitWidth() != 8 && m_bDownSampleTo8Bits)
    return 255;
  else
    return (m_pDataset->GetRange().first > m_pDataset->GetRange().second) ?
            m_p1DTrans->GetSize() : m_pDataset->GetRange().second;
}

bool AbstrRenderer::OnlyRecomposite(RenderRegion* region) const {
  return !region->isBlank &&
          m_bPerformReCompose &&
         !region->doAnotherRedrawDueToAllMeans;
}

vector<Brick> AbstrRenderer::BuildSubFrameBrickList(const RenderRegion& renderRegion,
                                                    bool bUseResidencyAsDistanceCriterion) {
  vector<Brick> vBrickList;

  UINTVECTOR3 vOverlap = m_pDataset->GetBrickOverlapSize();
  UINT64VECTOR3 vDomainSize = m_pDataset->GetDomainSize(m_iCurrentLOD);
  FLOATVECTOR3 vScale(m_pDataset->GetScale().x,
                      m_pDataset->GetScale().y,
                      m_pDataset->GetScale().z);

  FLOATVECTOR3 vDomainSizeCorrectedScale = vScale * FLOATVECTOR3(vDomainSize)/vDomainSize.maxVal();

  vScale /= vDomainSizeCorrectedScale.maxVal();

  FLOATVECTOR3 vBrickCorner;

  MESSAGE("Building active brick list from %u active bricks.",
          static_cast<unsigned>(m_pDataset->GetBrickCount(m_iCurrentLOD,
                                                          m_iTimestep)));

  BrickTable::const_iterator brick = m_pDataset->BricksBegin();
  for(; brick != m_pDataset->BricksEnd(); ++brick) {
    // skip over the brick if it's for the wrong timestep or LOD
    if(std::tr1::get<0>(brick->first) != m_iTimestep ||
       std::tr1::get<1>(brick->first) != m_iCurrentLOD) {
      continue;
    }
    const BrickMD& bmd = brick->second;
    Brick b;
    b.vExtension = bmd.extents * vScale;
    b.vCenter = bmd.center * vScale;
    b.vVoxelCount = bmd.n_voxels;
    b.kBrick = brick->first;


    // skip the brick if it is outside the current view frustum
    if (!m_FrustumCullingLOD.IsVisible(b.vCenter, b.vExtension)) {
      continue;
    }

    // skip the brick if it is clipped by the clipping plane
    if (m_bClipPlaneOn) {
      FLOATVECTOR3 vBrickVertices[8] = {
        b.vCenter + FLOATVECTOR3(-b.vExtension.x,-b.vExtension.y,-b.vExtension.z) * 0.5f,
        b.vCenter + FLOATVECTOR3(-b.vExtension.x,-b.vExtension.y,+b.vExtension.z) * 0.5f,
        b.vCenter + FLOATVECTOR3(-b.vExtension.x,+b.vExtension.y,-b.vExtension.z) * 0.5f,
        b.vCenter + FLOATVECTOR3(-b.vExtension.x,+b.vExtension.y,+b.vExtension.z) * 0.5f,
        b.vCenter + FLOATVECTOR3(+b.vExtension.x,-b.vExtension.y,-b.vExtension.z) * 0.5f,
        b.vCenter + FLOATVECTOR3(+b.vExtension.x,-b.vExtension.y,+b.vExtension.z) * 0.5f,
        b.vCenter + FLOATVECTOR3(+b.vExtension.x,+b.vExtension.y,-b.vExtension.z) * 0.5f,
        b.vCenter + FLOATVECTOR3(+b.vExtension.x,+b.vExtension.y,+b.vExtension.z) * 0.5f,
      };

      bool bClip = true;
      FLOATMATRIX4 matWorld = renderRegion.rotation * renderRegion.translation;
      for (size_t i = 0;i<8;i++) {
        vBrickVertices[i] = (FLOATVECTOR4(vBrickVertices[i],1) * matWorld)
                            .dehomo();
        if (!m_ClipPlane.Plane().clip(vBrickVertices[i])) {
          bClip = false;
          break;
        }
      }
      if (bClip) {
        continue;
      }
    }

    double fMaxValue = MaxValue();
    double fRescaleFactor = fMaxValue / double(m_p1DTrans->GetSize());

    // check if the brick contains any data worth rendering.
    bool bContainsData;
    switch (m_eRenderMode) {
      case RM_1DTRANS:
        bContainsData = m_pDataset->ContainsData(
                          brick->first,
                          double(m_p1DTrans->GetNonZeroLimits().x) * fRescaleFactor,
                          double(m_p1DTrans->GetNonZeroLimits().y) * fRescaleFactor
                        );
        break;
      case RM_2DTRANS:
        bContainsData = m_pDataset->ContainsData(
                          brick->first,
                          double(m_p2DTrans->GetNonZeroLimits().x) * fRescaleFactor,
                          double(m_p2DTrans->GetNonZeroLimits().y) * fRescaleFactor,
                          double(m_p2DTrans->GetNonZeroLimits().z),
                          double(m_p2DTrans->GetNonZeroLimits().w)
                        );
        break;
      case RM_ISOSURFACE:
        bContainsData = m_pDataset->ContainsData(brick->first, m_fIsovalue);
        break;
      default:
        bContainsData = false;
        break;
    }

    // skip the brick if no data are visible in the current rendering mode.
    if(!bContainsData) {
      MESSAGE("Skipping brick <%u,%u,%u> because it doesn't contain data "
              "under the current %s.",
              static_cast<unsigned>(std::tr1::get<0>(brick->first)),
              static_cast<unsigned>(std::tr1::get<1>(brick->first)),
              static_cast<unsigned>(std::tr1::get<2>(brick->first)),
              ((m_eRenderMode == RM_ISOSURFACE) ? "isovalue" : "tfqn"));
      continue;
    }

    bool first_x = m_pDataset->BrickIsFirstInDimension(0, brick->first);
    bool first_y = m_pDataset->BrickIsFirstInDimension(1, brick->first);
    bool first_z = m_pDataset->BrickIsFirstInDimension(2, brick->first);
    bool last_x = m_pDataset->BrickIsLastInDimension(0, brick->first);
    bool last_y = m_pDataset->BrickIsLastInDimension(1, brick->first);
    bool last_z = m_pDataset->BrickIsLastInDimension(2, brick->first);
    // compute texture coordinates
    if (m_bUseOnlyPowerOfTwo) {
      UINTVECTOR3 vRealVoxelCount(MathTools::NextPow2(b.vVoxelCount.x),
                                  MathTools::NextPow2(b.vVoxelCount.y),
                                  MathTools::NextPow2(b.vVoxelCount.z));
      b.vTexcoordsMin = FLOATVECTOR3(
        (first_x) ? 0.5f/vRealVoxelCount.x : vOverlap.x*0.5f/vRealVoxelCount.x,
        (first_y) ? 0.5f/vRealVoxelCount.y : vOverlap.y*0.5f/vRealVoxelCount.y,
        (first_z) ? 0.5f/vRealVoxelCount.z : vOverlap.z*0.5f/vRealVoxelCount.z
      );
      b.vTexcoordsMax = FLOATVECTOR3(
        (last_x) ? 1.0f-0.5f/vRealVoxelCount.x : 1.0f-vOverlap.x*0.5f/vRealVoxelCount.x,
        (last_y) ? 1.0f-0.5f/vRealVoxelCount.y : 1.0f-vOverlap.y*0.5f/vRealVoxelCount.y,
        (last_z) ? 1.0f-0.5f/vRealVoxelCount.z : 1.0f-vOverlap.z*0.5f/vRealVoxelCount.z
      );

      b.vTexcoordsMax -= FLOATVECTOR3(vRealVoxelCount - b.vVoxelCount) /
                         FLOATVECTOR3(vRealVoxelCount);
    } else {
      // compute texture coordinates
      b.vTexcoordsMin = FLOATVECTOR3(
        (first_x) ? 0.5f/b.vVoxelCount.x : vOverlap.x*0.5f/b.vVoxelCount.x,
        (first_y) ? 0.5f/b.vVoxelCount.y : vOverlap.y*0.5f/b.vVoxelCount.y,
        (first_z) ? 0.5f/b.vVoxelCount.z : vOverlap.z*0.5f/b.vVoxelCount.z
      );
      // for padded volume adjust texcoords
      b.vTexcoordsMax = FLOATVECTOR3(
        (last_x) ? 1.0f-0.5f/b.vVoxelCount.x : 1.0f-vOverlap.x*0.5f/b.vVoxelCount.x,
        (last_y) ? 1.0f-0.5f/b.vVoxelCount.y : 1.0f-vOverlap.y*0.5f/b.vVoxelCount.y,
        (last_z) ? 1.0f-0.5f/b.vVoxelCount.z : 1.0f-vOverlap.z*0.5f/b.vVoxelCount.z
      );
    }

    // the depth order doesn't really matter for MIP rotations,
    // since we need to traverse every brick anyway.  So we do a
    // sort based on which bricks are already resident, to get a
    // good cache hit rate.
    if (bUseResidencyAsDistanceCriterion) {
      if (m_pMasterController->MemMan()->IsResident(m_pDataset, brick->first,
                                                    m_bUseOnlyPowerOfTwo,
                                                    m_bDownSampleTo8Bits,
                                                    m_bDisableBorder)) {
        b.fDistance = 0;
      } else {
        b.fDistance = 1;
      }
    } else {
      // compute minimum distance to brick corners (offset
      // slightly to the center to resolve ambiguities)
      b.fDistance = brick_distance(b, renderRegion.modelView[0]);
    }

    // add the brick to the list of active bricks
    vBrickList.push_back(b);
  }

  // depth sort bricks
  sort(vBrickList.begin(), vBrickList.end());

  return vBrickList;
}

void AbstrRenderer::Plan3DFrame(RenderRegion3D& region) {
  if (region.isBlank) {
    // compute modelviewmatrix and pass it to the culling object
    region.modelView[0] = region.rotation*region.translation*m_mView[0];
    if (m_bDoStereoRendering)
      region.modelView[1] = region.rotation*region.translation*m_mView[1];

    // we assume that the left and right eye's view are similar so we only
    // use one for culling
    m_FrustumCullingLOD.SetViewMatrix(region.modelView[0]);
    m_FrustumCullingLOD.Update();

    // figure out how fine we need to draw the data for the current view
    // this method takes the size of a voxel in screen space into account
    ComputeMinLODForCurrentView();
    // figure out at what coarse level we need to start for the current view
    // this method takes the rendermode (capture or not) and the time it took
    // to render the last subframe into account
    ComputeMaxLODForCurrentView(region);
  }

  // plan if the frame is to be redrawn
  // or if we have completed the last subframe but not the entire frame
  if (region.isBlank ||
      (m_vCurrentBrickList.size() == m_iBricksRenderedInThisSubFrame)) {

    bool bBuildNewList = false;
    if (region.isBlank) {
      region.decreaseSamplingRateNow = region.decreaseSamplingRate;
      region.decreaseScreenResNow = region.decreaseScreenRes;
      bBuildNewList = true;
      if (region.decreaseSamplingRateNow || region.decreaseScreenResNow)
        region.doAnotherRedrawDueToAllMeans = true;
    } else {
      if (region.decreaseSamplingRateNow || region.decreaseScreenResNow) {
        region.decreaseScreenResNow = false;
        region.decreaseSamplingRateNow = false;
        m_iBricksRenderedInThisSubFrame = 0;
        region.doAnotherRedrawDueToAllMeans = false;
      } else {
        if (m_iCurrentLODOffset > m_iMinLODForCurrentView) {
          bBuildNewList = true;
          m_iCurrentLODOffset--;
        }
      }
    }

    if (bBuildNewList) {
      if(m_bCaptureMode) {
        m_iCurrentLOD = 0;
      } else {
        m_iCurrentLOD = std::min<UINT64>(m_iCurrentLODOffset,
                                         m_pDataset->GetLODLevelCount()-1);
      }
      // build new brick todo-list
      MESSAGE("Building new brick list for LOD %llu...", m_iCurrentLOD);
      m_vCurrentBrickList = BuildSubFrameBrickList(region);
      MESSAGE("%u bricks made the cut.", UINT32(m_vCurrentBrickList.size()));
      if (m_bDoStereoRendering) {
        m_vLeftEyeBrickList =
          BuildLeftEyeSubFrameBrickList(region, m_vCurrentBrickList);
      }

      m_iBricksRenderedInThisSubFrame = 0;
    }
  }

  if (region.isBlank) {
    // update frame states
    m_iIntraFrameCounter = 0;
    m_iFrameCounter = m_pMasterController->MemMan()->UpdateFrameCounter();
  }
}

void AbstrRenderer::PlanHQMIPFrame(RenderRegion& renderRegion) {
  // compute modelviewmatrix and pass it to the culling object
  renderRegion.modelView[0] = renderRegion.rotation*renderRegion.translation*m_mView[0];

  m_FrustumCullingLOD.SetPassAll(true);

  UINTVECTOR3  viVoxelCount = UINTVECTOR3(m_pDataset->GetDomainSize());

  m_iCurrentLODOffset = 0;
  m_iCurrentLOD = 0;

  if (m_bMIPLOD) {
    while (viVoxelCount.minVal() >= m_vWinSize.maxVal()) {
      viVoxelCount /= 2;
      m_iCurrentLOD++;
    }
  }

  if (m_iCurrentLOD > 0) {
    m_iCurrentLOD = min<int>(m_pDataset->GetLODLevelCount()-1,m_iCurrentLOD-1);
  }

  // build new brick todo-list
  m_vCurrentBrickList = BuildSubFrameBrickList(renderRegion, true);

  m_iBricksRenderedInThisSubFrame = 0;

  // update frame states
  m_iIntraFrameCounter = 0;
  m_iFrameCounter = m_pMasterController->MemMan()->UpdateFrameCounter();
}

void AbstrRenderer::SetCV(bool bEnable) {
  if (!SupportsClearView()) return;

  if (m_bDoClearView != bEnable) {
    m_bDoClearView = bEnable;
    if (m_eRenderMode == RM_ISOSURFACE)
      Schedule3DWindowRedraws();
  }
}

void AbstrRenderer::SetIsosufaceColor(const FLOATVECTOR3& vColor) {
  m_vIsoColor = vColor;
  if (m_eRenderMode == RM_ISOSURFACE)
    ScheduleRecompose();
}

void AbstrRenderer::SetOrthoView(bool bOrthoView) {
  if (m_bOrthoView != bOrthoView) {
    m_bOrthoView = bOrthoView;
    ScheduleCompleteRedraw();
  }
}

void AbstrRenderer::SetRenderCoordArrows(bool bRenderCoordArrows) {
  if (m_bRenderCoordArrows != bRenderCoordArrows) {
    m_bRenderCoordArrows = bRenderCoordArrows;
    Schedule3DWindowRedraws();
  }
}

void AbstrRenderer::Set2DPlanesIn3DView(bool bRenderPlanesIn3D,
                                        RenderRegion *renderRegion) {
  if (!renderRegion)
    renderRegion = GetFirst3DRegion();
  if (renderRegion) {
    if (m_bRenderPlanesIn3D != bRenderPlanesIn3D) {
      m_bRenderPlanesIn3D = bRenderPlanesIn3D;
      ScheduleWindowRedraw(renderRegion);
    }
  }
}

void AbstrRenderer::SetCVIsoValue(float fIsovalue) {
  if (m_fCVIsovalue != fIsovalue) {
    m_fCVIsovalue = fIsovalue;

    if (m_bDoClearView && m_eRenderMode == RM_ISOSURFACE) {
      Schedule3DWindowRedraws();
    }
    std::ostringstream prov;
    prov << fIsovalue;
    Controller::Instance().Provenance("cv", "setcviso", prov.str());
  }
}

void AbstrRenderer::SetCVColor(const FLOATVECTOR3& vColor) {
  if (m_vCVColor != vColor) {
    m_vCVColor = vColor;
    if (m_bDoClearView && m_eRenderMode == RM_ISOSURFACE)
      ScheduleRecompose();
  }
}

void AbstrRenderer::SetCVSize(float fSize) {
  if (m_fCVSize != fSize) {
    m_fCVSize = fSize;
    if (m_bDoClearView && m_eRenderMode == RM_ISOSURFACE)
      ScheduleRecompose();
  }
}

void AbstrRenderer::SetCVContextScale(float fScale) {
  if (m_fCVContextScale != fScale) {
    m_fCVContextScale = fScale;
    if (m_bDoClearView && m_eRenderMode == RM_ISOSURFACE)
      ScheduleRecompose();
  }
}

void AbstrRenderer::SetCVBorderScale(float fScale) {
  if (m_fCVBorderScale != fScale) {
    m_fCVBorderScale = fScale;
    if (m_bDoClearView && m_eRenderMode == RM_ISOSURFACE) {
      ScheduleRecompose();
    }
  }
}

void AbstrRenderer::SetCVFocusPos(RenderRegion& renderRegion, INTVECTOR2 vPos) {
  if (m_vCVMousePos!= vPos) {
    m_vCVMousePos = vPos;
    if (m_bDoClearView && m_eRenderMode == RM_ISOSURFACE)
      CVFocusHasChanged(renderRegion);
  }
}

void AbstrRenderer::SetLogoParams(string strLogoFilename, int iLogoPos) {
  m_strLogoFilename = strLogoFilename;
  m_iLogoPos        = iLogoPos;
}

void AbstrRenderer::Set2DFlipMode(RenderRegion *renderRegion,
                                  bool flipX, bool flipY) {
  renderRegion->SetFlipView(flipX, flipY);
  ScheduleWindowRedraw(renderRegion);
}

void AbstrRenderer::Get2DFlipMode(const RenderRegion *renderRegion,
                                  bool& flipX, bool& flipY) const {
  renderRegion->GetFlipView(flipX, flipY);
}

bool AbstrRenderer::GetUseMIP(const RenderRegion *renderRegion) const {
  return renderRegion->GetUseMIP();
}

void AbstrRenderer::SetUseMIP(RenderRegion *renderRegion, bool useMIP) {
  renderRegion->SetUseMIP(useMIP);
  ScheduleWindowRedraw(renderRegion);
}

void AbstrRenderer::SetStereo(bool bStereoRendering) {
  m_bRequestStereoRendering = bStereoRendering;
  Schedule3DWindowRedraws();
}

void AbstrRenderer::SetStereoEyeDist(float fStereoEyeDist) {
  m_fStereoEyeDist = fStereoEyeDist;
  if (m_bDoStereoRendering) Schedule3DWindowRedraws();
}

void AbstrRenderer::SetStereoFocalLength(float fStereoFocalLength) {
  m_fStereoFocalLength = fStereoFocalLength;
  if (m_bDoStereoRendering) Schedule3DWindowRedraws();
}

void AbstrRenderer::CVFocusHasChanged(RenderRegion &) {
  ScheduleRecompose();
}

void AbstrRenderer::SetConsiderPreviousDepthbuffer(bool bConsiderPreviousDepthbuffer) {
  if (m_bConsiderPreviousDepthbuffer != bConsiderPreviousDepthbuffer)
  {
    m_bConsiderPreviousDepthbuffer = bConsiderPreviousDepthbuffer;
    ScheduleCompleteRedraw();
  }
}

void AbstrRenderer::SetPerfMeasures(UINT32 iMinFramerate, bool bUseAllMeans,
                                    float fScreenResDecFactor,
                                    float fSampleDecFactor, UINT32 iStartDelay) {
  m_fMaxMSPerFrame = (iMinFramerate == 0) ? 10000 : 1000.0f / float(iMinFramerate);
  m_fScreenResDecFactor = fScreenResDecFactor;
  m_fSampleDecFactor = fSampleDecFactor;
  m_bUseAllMeans = bUseAllMeans;

  if (!m_bUseAllMeans) {
    for (size_t i=0; i < renderRegions.size(); ++i) {
      RenderRegion* region = renderRegions[i];
      region->decreaseScreenRes = false;
      region->decreaseScreenResNow = false;
      region->decreaseSamplingRate = false;
      region->decreaseSamplingRateNow = false;
      region->doAnotherRedrawDueToAllMeans = false;
    }
  }

  m_iStartDelay = iStartDelay;

  ScheduleCompleteRedraw();
}

void AbstrRenderer::SetLODLimits(const UINTVECTOR2 iLODLimits) {
  m_iLODLimits = iLODLimits;
  ScheduleCompleteRedraw();
}

void AbstrRenderer::SetColors(const FLOATVECTOR4& ambient,
                       const FLOATVECTOR4& diffuse,
                       const FLOATVECTOR4& specular) {
  m_cAmbient = ambient;
  m_cDiffuse = diffuse;
  m_cSpecular = specular;

  UpdateColorsInShaders();
  if (m_bUseLighting) Schedule3DWindowRedraws();
}

FLOATVECTOR4 AbstrRenderer::GetAmbient() const {
  return m_cAmbient;
}

FLOATVECTOR4 AbstrRenderer::GetDiffuse() const {
  return m_cDiffuse;
}

FLOATVECTOR4 AbstrRenderer::GetSpecular()const {
  return m_cSpecular;
}

void AbstrRenderer::Timestep(size_t t) { m_iTimestep = t; }
size_t AbstrRenderer::Timestep() const { return m_iTimestep; }
