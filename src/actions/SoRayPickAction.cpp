/**************************************************************************\
 *
 *  This file is part of the Coin 3D visualization library.
 *  Copyright (C) 1998-2000 by Systems in Motion. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  version 2.1 as published by the Free Software Foundation. See the
 *  file LICENSE.LGPL at the root directory of the distribution for
 *  more details.
 *
 *  If you want to use Coin for applications not compatible with the
 *  LGPL, please contact SIM to acquire a Professional Edition license.
 *
 *  Systems in Motion, Prof Brochs gate 6, 7030 Trondheim, NORWAY
 *  http://www.sim.no support@sim.no Voice: +47 22114160 Fax: +47 22207097
 *
\**************************************************************************/

/*!
  \class SoRayPickAction SoRayPickAction.h Inventor/actions/SoRayPickAction.h
  \brief The SoRayPickAction class does ray intersection with scene graphs.
  \ingroup actions

  For interaction with the scene graph geometry, it is necessary to be
  able to do intersection testing for rays. This functionality is
  provided by the SoRayPickAction class.
*/

#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/actions/SoSubActionP.h>

#include <Inventor/SbLine.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/elements/SoClipPlaneElement.h>
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoOverrideElement.h>
#include <Inventor/elements/SoPickRayElement.h>
#include <Inventor/elements/SoViewVolumeElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/lists/SoEnabledElementsList.h>
#include <Inventor/lists/SoPickedPointList.h>
#include <Inventor/misc/SoState.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoLOD.h>
#include <Inventor/nodes/SoLevelOfDetail.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShape.h>
#include <coindefs.h> // COIN_STUB()
#include <float.h>

#if COIN_DEBUG
#include <Inventor/errors/SoDebugError.h>
#endif // COIN_DEBUG


// *************************************************************************

// The private data for the SoRayPickAction.

class SoRayPickActionP {
public:
  SoRayPickActionP(SoRayPickAction * o) {
    this->owner = o;
  }

  // Hidden private methods.


  SbBool isBetweenPlanesWS(const SbVec3f & intersection,
                           const SoClipPlaneElement * planes) const;
  void cleanupPickedPoints(void);
  void setFlag(const unsigned int flag);
  void clearFlag(const unsigned int flag);
  SbBool isFlagSet(const unsigned int flag) const;
  void calcObjectSpaceData(SoState * ownerstate);
  void calcMatrices(SoState * ownerstate);
  // FIXME: unused method. 20010103 mortene.
  float calcRayRadius(const SbVec2s & size, const float radiusinpixels);

  // Hidden private variables.

  SbViewVolume osvolume;
  SbLine osline;
  SbPlane nearplane;
  SbVec2s vppoint;
  SbVec2f normvppoint;
  SbVec3f raystart;
  SbVec3f raydirection;
  double rayradiusstart;
  double rayradiusdelta;
  double raynear;
  double rayfar;
  float radiusinpixels;

  float currentPickDistance;

  SbLine wsline;
  SbMatrix obj2world;
  SbMatrix world2obj;
  SbMatrix extramatrix;

  SoPickedPointList pickedpointlist;

  unsigned int flags;

  enum {
    WS_RAY_SET =      0x0001, // ray set by ::setRay
    WS_RAY_COMPUTED = 0x0002, // ray computed in ::computeWorldSpaceRay
    PICK_ALL =        0x0004, // return all picked objects, or just closest
    NORM_POINT =      0x0008, // is normalized vppoint calculated
    CLIP_NEAR =       0x0010, // clip ray at near plane?
    CLIP_FAR =        0x0020, // clip ray at far plane?
    EXTRA_MATRIX =    0x0040 // is extra matrix supplied in ::setObjectSpace
  };

private:
  SoRayPickAction * owner;
};

#define THIS (this->pimpl)

// *************************************************************************

SO_ACTION_SOURCE(SoRayPickAction);


// Override from parent class.
void
SoRayPickAction::initClass(void)
{
  SO_ACTION_INIT_CLASS(SoRayPickAction, SoPickAction);

  SO_ENABLE(SoRayPickAction, SoPickRayElement);
  SO_ENABLE(SoRayPickAction, SoViewportRegionElement);
  SO_ENABLE(SoRayPickAction, SoOverrideElement);
}


/*!
  Constructor.

  Some node types need a \a viewportregion to know exactly how they
  are positioned within the scene.
*/
SoRayPickAction::SoRayPickAction(const SbViewportRegion & viewportregion)
  : inherited(viewportregion)
{
  THIS = new SoRayPickActionP(this);
  THIS->radiusinpixels = 5.0f;
  THIS->flags = 0;


  SO_ACTION_CONSTRUCTOR(SoRayPickAction);

  // most methods are inherited from SoPickAction
  SO_ACTION_ADD_METHOD_INTERNAL(SoCamera, SoNode::rayPickS);
  SO_ACTION_ADD_METHOD_INTERNAL(SoSeparator, SoNode::rayPickS);
  SO_ACTION_ADD_METHOD_INTERNAL(SoLOD, SoNode::rayPickS);
  SO_ACTION_ADD_METHOD_INTERNAL(SoLevelOfDetail, SoNode::rayPickS);
  SO_ACTION_ADD_METHOD_INTERNAL(SoShape, SoNode::rayPickS);
}

/*!
  Destructor, free temporary resources used by action.
*/
SoRayPickAction::~SoRayPickAction(void)
{
  THIS->cleanupPickedPoints();
  delete THIS;
}

/*!
  Sets the viewport-space point. This point is calculated into a line
  from the near clipping plane to the far clipping plane, and the
  intersection ray follows the line.

  This is a convenient way to detect object intersection below the
  cursor.
*/
void
SoRayPickAction::setPoint(const SbVec2s & viewportpoint)
{
  THIS->vppoint = viewportpoint;
  THIS->clearFlag(SoRayPickActionP::NORM_POINT |
                  SoRayPickActionP::WS_RAY_SET |
                  SoRayPickActionP::WS_RAY_COMPUTED);
  THIS->setFlag(SoRayPickActionP::CLIP_NEAR |
                SoRayPickActionP::CLIP_FAR);
}

/*!
  Sets the viewport-space point which the ray is sent through.
  The coordinate is normalized, ranging from (0, 0) to (1, 1).

  \sa setPoint()
*/
void
SoRayPickAction::setNormalizedPoint(const SbVec2f & normpoint)
{
  THIS->normvppoint = normpoint;
  THIS->clearFlag(SoRayPickActionP::WS_RAY_SET |
                  SoRayPickActionP::WS_RAY_COMPUTED);
  THIS->setFlag(SoRayPickActionP::NORM_POINT |
                SoRayPickActionP::CLIP_NEAR |
                SoRayPickActionP::CLIP_FAR);
}

/*!
  Sets the radius of the picking ray, in screen pixels.
*/
void
SoRayPickAction::setRadius(const float radiusinpixels)
{
  THIS->radiusinpixels = radiusinpixels;
}

/*!
  Sets the ray in world-space coordinates.
*/
void
SoRayPickAction::setRay(const SbVec3f & start, const SbVec3f & direction,
                        float neardistance, float fardistance)
{
  if (neardistance >= 0.0f) THIS->setFlag(SoRayPickActionP::CLIP_NEAR);
  else {
    THIS->clearFlag(SoRayPickActionP::CLIP_NEAR);
    neardistance = 0.0f;
  }

  if (fardistance >= 0.0f) THIS->setFlag(SoRayPickActionP::CLIP_FAR);
  else {
    THIS->clearFlag(SoRayPickActionP::CLIP_FAR);
    fardistance = neardistance + 1.0f;
  }

  // FIXME: when do I calculate these?
  THIS->rayradiusstart = 0.01f;
  THIS->rayradiusdelta = 0.0f;

  THIS->raystart = start;
  THIS->raydirection = direction;
  THIS->raydirection.normalize();
  THIS->raynear = neardistance;
  THIS->rayfar = fardistance;
  THIS->wsline = SbLine(start, start + direction);

  // D = shortest distance from origin to plane
  const float D = THIS->raydirection.dot(THIS->raystart);
  THIS->nearplane = SbPlane(THIS->raydirection, D + THIS->raynear);

  THIS->setFlag(SoRayPickActionP::WS_RAY_SET);
}

/*!
  Lets you decide whether only the closest object or all the objects
  the ray intersects with should be picked.
*/
void
SoRayPickAction::setPickAll(const SbBool flag)
{
  if (flag) THIS->setFlag(SoRayPickActionP::PICK_ALL);
  else THIS->clearFlag(SoRayPickActionP::PICK_ALL);
}

/*!
  Returns whether only the closest object or all the objects the ray
  intersects with is picked.
*/
SbBool
SoRayPickAction::isPickAll(void) const
{
  return THIS->isFlagSet(SoRayPickActionP::PICK_ALL);
}

/*!
  Returns a list of the picked points.
*/
const SoPickedPointList &
SoRayPickAction::getPickedPointList(void) const
{
  return THIS->pickedpointlist;
}

/*!
  Returns the picked point with \a index in the list of picked
  points.
*/
SoPickedPoint *
SoRayPickAction::getPickedPoint(const int index) const
{
  assert(index >= 0);
  if (index < THIS->pickedpointlist.getLength()) {
    return THIS->pickedpointlist[index];
  }
  return NULL;
}

/*!
  \internal
 */
void
SoRayPickAction::computeWorldSpaceRay(void)
{
  if (!THIS->isFlagSet(SoRayPickActionP::WS_RAY_SET)) {
    const SbViewVolume & vv = SoViewVolumeElement::get(this->state);

    if (!THIS->isFlagSet(SoRayPickActionP::NORM_POINT)) {

      SbVec2s pt = THIS->vppoint - this->vpRegion.getViewportOriginPixels();
      SbVec2s size = this->vpRegion.getViewportSizePixels();
      THIS->normvppoint.setValue(float(pt[0]) / float(size[0]),
                                 float(pt[1]) / float(size[1]));
      THIS->setFlag(SoRayPickActionP::NORM_POINT);

    }

#if COIN_DEBUG
    if (vv.getDepth() == 0.0f || vv.getWidth() == 0.0f || vv.getHeight() == 0.0f) {
      SoDebugError::postWarning("SoRayPickAction::computeWorldSpaceRay",
                                "invalid frustum: <%f, %f, %f>",
                                vv.getWidth(), vv.getHeight(), vv.getDepth());
      return;
    }
#endif // COIN_DEBUG

    SbLine templine;
    vv.projectPointToLine(THIS->normvppoint, templine);
    THIS->raystart = templine.getPosition();
    THIS->raydirection = templine.getDirection();

    THIS->raynear = 0.0;
    THIS->rayfar = vv.getDepth();

    SbVec2s vpsize = this->vpRegion.getViewportSizePixels();
    THIS->rayradiusstart = (double(vv.getHeight()) / double(vpsize[1]))*
      double(THIS->radiusinpixels);
    THIS->rayradiusdelta = 0.0;
    if (vv.getProjectionType() == SbViewVolume::PERSPECTIVE) {
      SbVec3f dir(0.0f, vv.getHeight()*0.5f, vv.getNearDist());
      dir.normalize();
      SbVec3f upperfar = dir * (vv.getNearDist()+vv.getDepth()) /
        dir.dot(SbVec3f(0.0f, 0.0f, 1.0f));

      double farheight = double(upperfar[1])*2.0;
      double farsize = (farheight / double(vpsize[1])) * double(THIS->radiusinpixels);
      THIS->rayradiusdelta = (farsize - THIS->rayradiusstart) / double(vv.getDepth());
    }
    THIS->wsline = SbLine(THIS->raystart,
                          THIS->raystart + THIS->raydirection);

    THIS->nearplane = SbPlane(vv.getProjectionDirection(), THIS->raystart);
    THIS->setFlag(SoRayPickActionP::WS_RAY_COMPUTED);
  }
}

/*!
  \internal
 */
SbBool
SoRayPickAction::hasWorldSpaceRay(void) const
{
  return THIS->isFlagSet(SoRayPickActionP::WS_RAY_SET|SoRayPickActionP::WS_RAY_COMPUTED);
}

/*!
  \internal
 */
void
SoRayPickAction::setObjectSpace(void)
{
  THIS->clearFlag(SoRayPickActionP::EXTRA_MATRIX);
  THIS->calcObjectSpaceData(this->state);
}

/*!
  \internal
 */
void
SoRayPickAction::setObjectSpace(const SbMatrix & matrix)
{
  THIS->setFlag(SoRayPickActionP::EXTRA_MATRIX);
  THIS->extramatrix = matrix;
  THIS->calcObjectSpaceData(this->state);
}

/*!
  \internal
 */
SbBool
SoRayPickAction::intersect(const SbVec3f & v0,
                           const SbVec3f & v1,
                           const SbVec3f & v2,
                           SbVec3f & intersection, SbVec3f & barycentric,
                           SbBool & front) const
{
  const SbVec3f & orig = THIS->osline.getPosition();
  const SbVec3f & dir = THIS->osline.getDirection();

  SbVec3f edge1 = v1 - v0;
  SbVec3f edge2 = v2 - v0;

  SbVec3f pvec = dir.cross(edge2);

  // if determinant is near zero, ray lies in plane of triangle
  float det = edge1.dot(pvec);
  if (fabs(det) < FLT_EPSILON) return FALSE;

  // does ray hit front or back of triangle
  if (det > 0.0f) front = TRUE;
  else front = FALSE;

  // create some more intuitive barycentric coordinate names
  float & u = barycentric[1];
  float & v = barycentric[2];
  float & w = barycentric[0];

  float inv_det = 1.0f / det;

  // calculate distance from v0 to ray origin
  SbVec3f tvec = orig - v0;

  // calculate U parameter and test bounds
  u = tvec.dot(pvec) * inv_det;
  if (u < 0.0f || u > 1.0f)
    return FALSE;

  // prepare to test V parameter
  SbVec3f qvec = tvec.cross(edge1);

  // calculate V parameter and test bounds
  v = dir.dot(qvec) * inv_det;
  if (v < 0.0f || u + v > 1.0f)
    return FALSE;

  // third barycentric coordinate
  w = 1.0f - u - v;

  // calculate t and intersection point
  float t = edge2.dot(qvec) * inv_det;
  intersection = orig + t * dir;

  return TRUE;
}

/*!
  \internal
 */
SbBool
SoRayPickAction::intersect(const SbVec3f & v0, const SbVec3f & v1,
                           SbVec3f & intersection) const
{
  SbLine line(v0, v1);
  SbVec3f op0, op1; // object space
  SbVec3f p0, p1; // world space

  THIS->osline.getClosestPoints(line, op0, op1);

  // clamp op1 between v0 and v1
  if ((op1-v0).dot(line.getDirection()) < 0.0f) op1 = v0;
  else if ((v1-op1).dot(line.getDirection()) < 0.0f) op1 = v1;

  // FIXME: clamp op0 to raystart, rayfar ???

  THIS->obj2world.multVecMatrix(op0, p0);
  THIS->obj2world.multVecMatrix(op1, p1);

  // distance between points
  float distance = (p1-p0).length();

  float raypos = THIS->nearplane.getDistance(p0);

  float radius = THIS->rayradiusstart +
    THIS->rayradiusdelta * raypos;

  if (radius >= distance) {
    intersection = op1;
    return TRUE;
  }
  return FALSE;
}

/*!
  \internal
 */
SbBool
SoRayPickAction::intersect(const SbVec3f & point) const
{
  SbVec3f wpoint;
  THIS->obj2world.multVecMatrix(point, wpoint);
  SbVec3f ptonline = THIS->wsline.getClosestPoint(wpoint);

  // distance between points
  float distance = (wpoint-ptonline).length();

  float raypos = THIS->nearplane.getDistance(ptonline);

  float radius = THIS->rayradiusstart +
    THIS->rayradiusdelta * raypos;

  return (radius >= distance);
}

/*!
  \internal
*/
SbBool
SoRayPickAction::intersect(const SbBox3f & box, SbVec3f & intersection,
                           const SbBool usefullviewvolume)
{
  // FIXME: usefullviewvolume == TRUE is not supported.
  // pederb, 20000519
  const SbLine & line = THIS->osline;
  SbVec3f bounds[2];
  bounds[0] = box.getMin();
  bounds[1] = box.getMax();

  for (int j = 0; j < 2; j++) {
    for (int i = 0; i < 3; i++) {
      SbVec3f norm(0, 0, 0);
      norm[i] = 1.0f;
      SbVec3f isect;

      SbPlane plane(norm, bounds[j][i]);
      if (plane.intersect(line, isect)) {
        int i1 = (i+1) % 3;
        int i2 = (i+2) % 3;
        if (isect[i1] >= bounds[0][i1] && isect[i1] <= bounds[1][i1] &&
            isect[i2] >= bounds[0][i2] && isect[i2] <= bounds[1][i2]) {
          intersection = isect;
          return TRUE;
        }
      }
    }
  }
  return FALSE;
}


/*!
  \internal
 */
SbBool
SoRayPickAction::intersect(const SbBox3f & box, const SbBool usefullviewvolume)
{
  SbVec3f dummy;
  return this->intersect(box, dummy, usefullviewvolume);
}

/*!
  \internal
 */
const SbViewVolume &
SoRayPickAction::getViewVolume(void)
{
  COIN_STUB();
  return THIS->osvolume;
}

/*!
  \internal
 */
const SbLine &
SoRayPickAction::getLine(void)
{
  return THIS->osline;
}

/*!
  \internal
 */
SbBool
SoRayPickAction::isBetweenPlanes(const SbVec3f & intersection) const
{
  SbVec3f worldpoint;
  THIS->obj2world.multVecMatrix(intersection, worldpoint);
  return THIS->isBetweenPlanesWS(worldpoint,
                                 SoClipPlaneElement::getInstance(this->state));
}

/*!
  \internal
 */
SoPickedPoint *
SoRayPickAction::addIntersection(const SbVec3f & objectspacepoint)
{
  SbVec3f worldpoint;
  THIS->obj2world.multVecMatrix(objectspacepoint, worldpoint);

  if (THIS->pickedpointlist.getLength() && !THIS->isFlagSet(SoRayPickActionP::PICK_ALL)) {
    // got to test if new candidate is closer than old one
    float dist = THIS->nearplane.getDistance(worldpoint);
    if (dist >= THIS->currentPickDistance) return NULL; // farther

    // remove old point
    delete THIS->pickedpointlist[0];
    THIS->pickedpointlist.truncate(0);
  }

  if (!THIS->isFlagSet(SoRayPickActionP::PICK_ALL)) {
    THIS->currentPickDistance = THIS->nearplane.getDistance(worldpoint);
  }
  // create the new picked point
  SoPickedPoint * pp = new SoPickedPoint(this->getCurPath(),
                                         this->state, objectspacepoint);
  THIS->pickedpointlist.append(pp);
  return pp;
}

/*!
  Overloaded to set up internal data.
 */
void
SoRayPickAction::beginTraversal(SoNode * node)
{
  THIS->cleanupPickedPoints();
  this->getState()->push();
  SoViewportRegionElement::set(this->getState(), this->vpRegion);
  inherited::beginTraversal(node);
  this->getState()->pop();
}


//////// Hidden private methods for //////////////////////////////////////
//////// SoRayPickActionP (pimpl) ////////////////////////////////////////

SbBool
SoRayPickActionP::isBetweenPlanesWS(const SbVec3f & intersection,
                                    const SoClipPlaneElement * planes) const
{
  float dist = this->nearplane.getDistance(intersection);
  if (this->isFlagSet(CLIP_NEAR)) {
    if (dist < 0) return FALSE;
  }
  if (this->isFlagSet(CLIP_FAR)) {
    if (dist > (this->rayfar - this->raynear)) return FALSE;
  }
  int n =  planes->getNum();
  for (int i = 0; i < n; i++) {
    if (!planes->get(i).isInHalfSpace(intersection)) return FALSE;
  }
  return TRUE;
}

void
SoRayPickActionP::cleanupPickedPoints(void)
{
  int n = this->pickedpointlist.getLength();

  for (int i = 0; i < n; i++) {
    delete this->pickedpointlist[i];
  }
  this->pickedpointlist.truncate(0);
}

void
SoRayPickActionP::setFlag(const unsigned int flag)
{
  this->flags |= flag;
}

void
SoRayPickActionP::clearFlag(const unsigned int flag)
{
  this->flags &= ~flag;
}

SbBool
SoRayPickActionP::isFlagSet(const unsigned int flag) const
{
  return (this->flags & flag) != 0;
}

void
SoRayPickActionP::calcObjectSpaceData(SoState * ownerstate)
{
  this->calcMatrices(ownerstate);

  SbVec3f start, dir;

  this->world2obj.multVecMatrix(this->raystart, start);
  this->world2obj.multDirMatrix(this->raydirection, dir);
  this->osline = SbLine(start, start + dir);

  // FIXME: calc this->osvolume
}

void
SoRayPickActionP::calcMatrices(SoState * state)
{
  this->obj2world = SoModelMatrixElement::get(state);
  if (this->isFlagSet(EXTRA_MATRIX)) {
    this->obj2world.multLeft(this->extramatrix);
  }
  this->world2obj = this->obj2world.inverse();
}

float
SoRayPickActionP::calcRayRadius(const SbVec2s & vpsize,
                                const float radiusinpixels)
{
  // FIXME: unused method. 20010103 mortene.

  float xsize = float(vpsize[0]);
  float ysize = float(vpsize[1]);

  return float(radiusinpixels / sqrt(xsize * xsize + ysize * ysize));
}
