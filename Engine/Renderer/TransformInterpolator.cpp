#include "TransformInterpolator.h"

#include <algorithm>

using namespace DirectX;

PhysicsBodySnapshot TransformInterpolator::Interpolate(
    const PhysicsBodySnapshot& previous,
    const PhysicsBodySnapshot& current,
    float alpha)
{
    // Clamp because render timing can briefly drift outside the ideal range.
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    PhysicsBodySnapshot result = current;

    XMVECTOR prevPosition = XMLoadFloat3(&previous.Position);
    XMVECTOR currPosition = XMLoadFloat3(&current.Position);
    XMVECTOR prevRotation = XMLoadFloat4(&previous.Rotation);
    XMVECTOR currRotation = XMLoadFloat4(&current.Rotation);
    XMVECTOR prevScale = XMLoadFloat3(&previous.Scale);
    XMVECTOR currScale = XMLoadFloat3(&current.Scale);

    // Position/scale use linear interpolation; rotation uses slerp to avoid
    // visible snapping between fixed physics ticks.
    XMStoreFloat3(&result.Position, XMVectorLerp(prevPosition, currPosition, alpha));
    XMStoreFloat4(&result.Rotation, XMQuaternionSlerp(prevRotation, currRotation, alpha));
    XMStoreFloat3(&result.Scale, XMVectorLerp(prevScale, currScale, alpha));

    return result;
}
