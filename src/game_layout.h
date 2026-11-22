#pragma once
#include "memory.h"

// Offsets, flag bits and vtable slots of the verified baseline core. Names follow the game's own symbols
// wherever there is one. Offsets past GameObject's own fields belong to one class: GameObjectInteractive's
// +0x174 is not GameObjectCinematic's +0x174.
namespace layout {

namespace world {  // GameWorld
constexpr u32 kSceneRoot = 0x08;     // scene root node (the space regions are expressed in)
constexpr u32 kCamera = 0x28;        // CameraController*
constexpr u32 kObjectsBegin = 0x58;  // vector<shared_ptr<GameObject>> of every object (GameWorld::getObjectIndex)
constexpr u32 kObjectsEnd = 0x5c;
constexpr u32 kObjectStride = 12;    // bytes per element
}  // namespace world

namespace game_object {  // GameObject
constexpr u32 kWorld = 0x10;         // owning GameWorld*
constexpr u32 kFlags = 0x48;         // flag word
constexpr u32 kBody = 0x58;          // physics body (some objects, such as triggers and markers, have none)
constexpr u32 kBodyShape = 0xc0;     // in the body: its collision shape

constexpr u32 kFlagDestroyed = 0x1;  // set by GameObject::destroy()
constexpr u32 kFlagHidden = 0x8;     // hidden / disabled
constexpr u32 kFlagInRange = 0x10;   // "the game says in range"; not a plain distance test
constexpr u32 kFlagSkipShape = 0x100;  // getBounds does not need the collision shape
}  // namespace game_object

namespace slot {  // vtable slots: byte offsets into the vtable
constexpr u32 kOnEvent = 0x08;             // GameObject::onEvent(const Event&); send_use calls it with an ActionEvent
constexpr u32 kIsInteractive = 0x48;      // GameObject::isInteractive() const
constexpr u32 kIsInteractivePlus = 0x4c;  // GameObject::isInteractivePlus() const
constexpr u32 kIsTelekinesisable = 0x5c;  // GameObject::isTelekinesisable() const
constexpr u32 kInitPostSpawn = 0x70;      // GameObject::initPostSpawn(spawn zone, plain spawn)
constexpr u32 kGetPos = 0xc4;             // GameObject::getPos() const
constexpr u32 kGetBounds = 0xe8;          // GameObject::getBounds() const (sret)
constexpr u32 kOnInteract = 0x170;        // GameObjectInteractive::onInteract()
}  // namespace slot

namespace interactive {  // GameObjectInteractive
constexpr u32 kUseEnabled = 0x174;    // byte: the object takes "use" (onOpen) at all
constexpr u32 kUseReach = 0x178;      // float, metres from the object's origin: the game's own interaction radius
constexpr u32 kUnknown17c = 0x17c;    // three bytes (+0x17c, +0x17d, +0x17e); lockers read 1,1,1, panels 1,0,0; meaning unknown
constexpr u32 kInputRegions = 0x190;  // vector of model nodes placed where the game shows the tap icon (begin, end at +0x194)
}  // namespace interactive

}  // namespace layout
