#include "Gameplay/AttackData.h"

#include "Core/Json.h"

void ReadAttack(const JsonValue& v, AttackData& a)
{
    if (v.IsNull())
        return;   // 그 공격은 파일에 없다. 기본값 그대로 둔다.

    a.startup  = v["startup"] .Int(a.startup);
    a.active   = v["active"]  .Int(a.active);
    a.recovery = v["recovery"].Int(a.recovery);

    a.reach          = v["reach"]         .Flt(a.reach);
    a.width          = v["width"]         .Flt(a.width);
    a.height         = v["height"]        .Flt(a.height);
    a.heightFromFoot = v["heightFromFoot"].Flt(a.heightFromFoot);

    a.damage      = v["damage"]     .Int(a.damage);
    a.staminaCost = v["staminaCost"].Int(a.staminaCost);
    a.impact      = v["impact"]     .Int(a.impact);

    ReadClip(v["clip"], a.clip);

    // 이 공격을 내는 동안의 몸 자세.
    //   글자로 적는다 — 숫자로 두면 파일만 보고 뜻을 알 수 없다.
    const std::string posture = v["posture"].Str();
    if      (posture == "prone")  a.posture = Posture::Prone;
    else if (posture == "crouch") a.posture = Posture::Crouch;
    else if (posture == "stand")  a.posture = Posture::Stand;

    // name 은 일부러 안 읽는다. const char* 라 수명 문제가 생기고,
    // 무엇보다 이름은 조정하는 값이 아니라 식별자다(WeaponType.h 참조).
}


void ReadClip(const JsonValue& v, AnimationClip& c)
{
    if (v.IsNull())
        return;   // 안 적었으면 기본값 그대로

    c.row           = v["row"]   .Int(c.row);
    c.frameCount    = v["frames"].Int(c.frameCount);
    c.ticksPerFrame = v["ticks"] .Int(c.ticksPerFrame);
    c.loop          = v["loop"]  .Bool(c.loop);
}
