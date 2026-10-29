#ifndef MOD_HEARTHSTONE_HS_ALLIANCE_SHAMAN_ROUTES_H
#define MOD_HEARTHSTONE_HS_ALLIANCE_SHAMAN_ROUTES_H
#include <array>
#include <cstdint>
// Owner-authored route snapshot, read as DOUBLE on 2026-10-06. Labels are
// normalized to consecutive runtime points so pause/exit checks cannot depend
// on sparse database numbering. Coordinates and their order are unchanged.
struct HsAllianceShamanPoint { uint32_t label; float x; float y; float z; };
constexpr std::array<HsAllianceShamanPoint, 9> kHsShamanJohnRoute = {{
    { 1, -4657.046875f, -1186.84716796875f, 501.6517028808594f },
    { 2, -4666.61279296875f, -1183.6138916015625f, 501.6584167480469f },
    { 3, -4675.3837890625f, -1178.490478515625f, 501.6430969238281f },
    { 4, -4682.17431640625f, -1174.606201171875f, 501.646728515625f },
    { 5, -4688.23046875f, -1170.3092041015625f, 501.6645812988281f },
    { 6, -4679.091796875f, -1172.803955078125f, 501.6323547363281f },
    { 7, -4673.0439453125f, -1180.228515625f, 501.64361572265625f },
    { 8, -4663.8251953125f, -1185.32275390625f, 501.657958984375f },
    { 9, -4654.77294921875f, -1182.329345703125f, 501.6465759277344f },
}};
constexpr HsAllianceShamanPoint kHsShamanJohnEntrance = { 0, -4651.2001953125f, -1180.8699951171875f, 501.64300537109375f };
constexpr float kHsShamanJohnFacing = 4.253230094909668f;
// The temporary Ikae chair sits beside John at his first demonstration stop.
// It is summoned only for the active owner's scene and removed before he moves
// to the keg.
constexpr HsAllianceShamanPoint kHsShamanChairPosition = { 0, -4689.4473f, -1169.4399f, 501.6646f };
constexpr float kHsShamanChairFacing = 2.5250f;
constexpr std::array<HsAllianceShamanPoint, 10> kHsShamanKiblerRoute = {{
    { 1, -4692.46875f, -1209.7388916015625f, 501.6595458984375f },
    { 2, -4688.43115234375f, -1202.3201904296875f, 501.6595458984375f },
    { 3, -4682.93017578125f, -1190.9234619140625f, 501.6595458984375f },
    { 4, -4678.8955078125f, -1184.436279296875f, 501.6595458984375f },
    { 5, -4678.3203125f, -1179.8858642578125f, 501.6554870605469f },
    { 6, -4682.0703125f, -1175.693359375f, 501.65032958984375f },
    { 7, -4671.72265625f, -1179.9686279296875f, 501.6406555175781f },
    { 8, -4661.53173828125f, -1184.0859375f, 501.6580810546875f },
    { 9, -4651.38671875f, -1185.02587890625f, 501.6521301269531f },
    { 10, -4640.84423828125f, -1176.9288330078125f, 501.6474609375f },
}};
constexpr HsAllianceShamanPoint kHsShamanKiblerEntrance = { 0, -4690.25f, -1213.8299560546875f, 501.6600036621094f };
constexpr float kHsShamanKiblerFacing = 1.8483200073242188f;
constexpr std::array<HsAllianceShamanPoint, 11> kHsShamanKripRoute = {{
    { 1, -4673.74951171875f, -1166.6917724609375f, 510.50787353515625f },
    { 2, -4675.4697265625f, -1170.57666015625f, 510.50958251953125f },
    { 3, -4669.26953125f, -1174.6724853515625f, 507.9234924316406f },
    { 4, -4659.46484375f, -1180.8695068359375f, 501.6580505371094f },
    { 5, -4661.49365234375f, -1185.8897705078125f, 501.6580505371094f },
    { 6, -4669.875f, -1185.317138671875f, 501.6580505371094f },
    { 7, -4675.03564453125f, -1182.66357421875f, 501.65673828125f },
    { 8, -4680.40625f, -1189.5655517578125f, 501.6587829589844f },
    { 9, -4695.89306640625f, -1198.19873046875f, 501.6587829589844f },
    { 10, -4706.44189453125f, -1197.9512939453125f, 501.6587829589844f },
    { 11, -4719.2763671875f, -1186.7906494140625f, 501.69647216796875f },
}};
constexpr HsAllianceShamanPoint kHsShamanKripEntrance = { 0, -4670.9501953125f, -1161.9200439453125f, 510.50799560546875f };
constexpr float kHsShamanKripFacing = 4.134610176086426f;
#endif
