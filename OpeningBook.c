#include "OpeningBook.h"

#include <stddef.h>
#include <stdlib.h>

typedef struct
{
    uint64_t hash;
    Move nextMove;
} Opening;

// To speed up finding matching openings, the hashes are sorted.
// So we can do a simple binary search for a matching hash.

// Note: the list below is auto-generated.
#define NUM_OPENINGS 230
#define MAX_OPENING_BOOK_PLY 15

static const Opening s_openingBook[NUM_OPENINGS] =
{
    { 0x0068900f083a329d, { SquareE7, SquareE6, Pawn, None } },
    { 0x0068900f083a329d, { SquareG7, SquareG6, Pawn, None } },
    { 0x00d15f7a6f3c368a, { SquareD5, SquareE4, Pawn, None } },
    { 0x023e30bf1e6ea6da, { SquareE5, SquareD4, Pawn, None } },
    { 0x02aafd2cf5396fac, { SquareF6, SquareE4, Knight, None } },
    { 0x02aafd2cf5396fac, { SquareF8, SquareE7, Bishop, None } },
    { 0x02aafd2cf5396fac, { SquareB7, SquareB5, Pawn, None } },
    { 0x0417ba4416f6b10c, { SquareG8, SquareF6, Knight, None } },
    { 0x0abd12f9376b7d4c, { SquareC8, SquareF5, Bishop, None } },
    { 0x0bc01e0256bf9d7d, { SquareF8, SquareG7, Bishop, None } },
    { 0x0c884c93d9b48464, { SquareD2, SquareD4, Pawn, None } },
    { 0x0c884c93d9b48464, { SquareG1, SquareF3, Knight, None } },
    { 0x0e1d65f30a2c6748, { SquareG8, SquareF6, Knight, None } },
    { 0x0e1d65f30a2c6748, { SquareF8, SquareC5, Bishop, None } },
    { 0x0eccda80b92ba590, { SquareE7, SquareE5, Pawn, None } },
    { 0x0eccda80b92ba590, { SquareE7, SquareE6, Pawn, None } },
    { 0x0eccda80b92ba590, { SquareC7, SquareC6, Pawn, None } },
    { 0x0eccda80b92ba590, { SquareC7, SquareC5, Pawn, None } },
    { 0x0eccda80b92ba590, { SquareD7, SquareD5, Pawn, None } },
    { 0x0f42e23c1cf0c863, { SquareD7, SquareD5, Pawn, None } },
    { 0x1520742dd576bc5a, { SquareD7, SquareD5, Pawn, None } },
    { 0x155b904855fb550e, { SquareE7, SquareE5, Pawn, None } },
    { 0x155b904855fb550e, { SquareE7, SquareE6, Pawn, None } },
    { 0x155b904855fb550e, { SquareC7, SquareC6, Pawn, None } },
    { 0x155b904855fb550e, { SquareD5, SquareC4, Pawn, None } },
    { 0x1c9bdde04a950c1e, { SquareD2, SquareD4, Pawn, None } },
    { 0x1d21dd4b0b908f71, { SquareD7, SquareD5, Pawn, None } },
    { 0x1f29998915efd31e, { SquareE4, SquareE5, Pawn, None } },
    { 0x20dec6f015790c35, { SquareE7, SquareE6, Pawn, None } },
    { 0x20dec6f015790c35, { SquareG7, SquareG6, Pawn, None } },
    { 0x22a50b49ab4f1900, { SquareD7, SquareD6, Pawn, None } },
    { 0x22a50b49ab4f1900, { SquareF8, SquareC5, Bishop, None } },
    { 0x22a50b49ab4f1900, { SquareG8, SquareF6, Knight, None } },
    { 0x22c42a12aa1628ff, { SquareE2, SquareE3, Pawn, None } },
    { 0x22c42a12aa1628ff, { SquareA2, SquareA3, Pawn, None } },
    { 0x22c42a12aa1628ff, { SquareD1, SquareC2, Queen, None } },
    { 0x22c42a12aa1628ff, { SquareG1, SquareF3, Knight, None } },
    { 0x2625e3190ea278fe, { SquareG8, SquareF6, Knight, None } },
    { 0x2625e3190ea278fe, { SquareF8, SquareC5, Bishop, None } },
    { 0x2625e3190ea278fe, { SquareB8, SquareC6, Knight, None } },
    { 0x263dfc4589c8b46a, { SquareE7, SquareE6, Pawn, None } },
    { 0x263dfc4589c8b46a, { SquareB8, SquareC6, Knight, None } },
    { 0x263dfc4589c8b46a, { SquareD7, SquareD6, Pawn, None } },
    { 0x273429df8582ec78, { SquareC2, SquareC4, Pawn, None } },
    { 0x273429df8582ec78, { SquareE2, SquareE4, Pawn, None } },
    { 0x273429df8582ec78, { SquareG1, SquareF3, Knight, None } },
    { 0x273429df8582ec78, { SquareD2, SquareD4, Pawn, None } },
    { 0x27bcc805733466bd, { SquareF8, SquareG7, Bishop, None } },
    { 0x27bcc805733466bd, { SquareD7, SquareD5, Pawn, None } },
    { 0x283a8936916a8916, { SquareA4, SquareB3, Bishop, None } },
    { 0x29e3c5d6fa83dada, { SquareF1, SquareD3, Bishop, None } },
    { 0x29e3c5d6fa83dada, { SquareB1, SquareC3, Knight, None } },
    { 0x2b9ce14086db2964, { SquareD2, SquareD4, Pawn, None } },
    { 0x2cb0c1df219b32d1, { SquareC7, SquareC5, Pawn, None } },
    { 0x2cb0c1df219b32d1, { SquareE7, SquareE5, Pawn, None } },
    { 0x2cb0c1df219b32d1, { SquareG8, SquareF6, Knight, None } },
    { 0x2fdc9cc8851b63d2, { SquareD5, SquareD6, Queen, None } },
    { 0x2fdc9cc8851b63d2, { SquareD5, SquareD8, Queen, None } },
    { 0x2fdc9cc8851b63d2, { SquareD5, SquareA5, Queen, None } },
    { 0x31550561fe85d84b, { SquareB8, SquareC6, Knight, None } },
    { 0x31550561fe85d84b, { SquareA7, SquareA6, Pawn, None } },
    { 0x31550561fe85d84b, { SquareG7, SquareG6, Pawn, None } },
    { 0x375c6f724dc62b1b, { SquareF7, SquareF5, Pawn, None } },
    { 0x381e38a30057b62e, { SquareB7, SquareC6, Pawn, None } },
    { 0x3cd88772e9dff5b2, { SquareE2, SquareE4, Pawn, None } },
    { 0x3cd88772e9dff5b2, { SquareC2, SquareC4, Pawn, None } },
    { 0x417f466b4f25d57e, { SquareB1, SquareC3, Knight, None } },
    { 0x417f466b4f25d57e, { SquareG1, SquareF3, Knight, None } },
    { 0x43a873126269b693, { SquareD7, SquareD6, Pawn, None } },
    { 0x43a873126269b693, { SquareG7, SquareG5, Pawn, None } },
    { 0x471e371df55ed24b, { SquareD2, SquareD4, Pawn, None } },
    { 0x47f6e517776f9f30, { SquareD7, SquareC6, Pawn, None } },
    { 0x48b5463b47ffbe86, { SquareF2, SquareF4, Pawn, None } },
    { 0x48b5463b47ffbe86, { SquareG2, SquareG3, Pawn, None } },
    { 0x499fb7642380b783, { SquareB1, SquareC3, Knight, None } },
    { 0x499fb7642380b783, { SquareE4, SquareE5, Pawn, None } },
    { 0x499fb7642380b783, { SquareB1, SquareD2, Knight, None } },
    { 0x4a8b9aa597e06b3c, { SquareF6, SquareE4, Knight, None } },
    { 0x4c35ae6be212df78, { SquareD2, SquareD3, Pawn, None } },
    { 0x4c35ae6be212df78, { SquareB1, SquareC3, Knight, None } },
    { 0x4d699ec5f5559c6a, { SquareG1, SquareF3, Knight, None } },
    { 0x527319c94fddae49, { SquareE4, SquareD5, Pawn, None } },
    { 0x5478300f3bb4c30b, { SquareB8, SquareC6, Knight, None } },
    { 0x59c218f8db51e35c, { SquareE7, SquareE5, Pawn, None } },
    { 0x59c218f8db51e35c, { SquareC7, SquareC5, Pawn, None } },
    { 0x59c218f8db51e35c, { SquareD7, SquareD5, Pawn, None } },
    { 0x5c15d90248a5d605, { SquareF3, SquareD4, Knight, None } },
    { 0x5d8d2883248aa78f, { SquareA4, SquareB3, Bishop, None } },
    { 0x5e24132a131c3118, { SquareD7, SquareD5, Pawn, None } },
    { 0x604290375dfbca57, { SquareD2, SquareD4, Pawn, None } },
    { 0x604290375dfbca57, { SquareF1, SquareC4, Bishop, None } },
    { 0x604290375dfbca57, { SquareF1, SquareB5, Bishop, None } },
    { 0x608dc0d14371a130, { SquareD4, SquareC6, Knight, None } },
    { 0x60bd27cb4235323e, { SquareB1, SquareC3, Knight, None } },
    { 0x6218e9b190e9421c, { SquareG8, SquareF6, Knight, None } },
    { 0x6218e9b190e9421c, { SquareD8, SquareD5, Queen, None } },
    { 0x6639555b34ae5498, { SquareC5, SquareD4, Pawn, None } },
    { 0x67274148729570df, { SquareB5, SquareA4, Bishop, None } },
    { 0x67274148729570df, { SquareB5, SquareC6, Bishop, None } },
    { 0x676aa2619cf8cadc, { SquareB1, SquareC3, Knight, None } },
    { 0x676aa2619cf8cadc, { SquareG1, SquareF3, Knight, None } },
    { 0x67a7c4cbbd7ccbfa, { SquareG2, SquareG3, Pawn, None } },
    { 0x683056e3e77891ed, { SquareD7, SquareD6, Pawn, None } },
    { 0x6a871f665fab7774, { SquareG1, SquareF3, Knight, None } },
    { 0x6a871f665fab7774, { SquareF1, SquareG2, Bishop, None } },
    { 0x6b62684f848625f5, { SquareF4, SquareE5, Pawn, None } },
    { 0x7230435bec9757e5, { SquareC2, SquareC4, Pawn, None } },
    { 0x7230435bec9757e5, { SquareG1, SquareF3, Knight, None } },
    { 0x7230435bec9757e5, { SquareC1, SquareF4, Bishop, None } },
    { 0x728c8c6c5a2a8d7f, { SquareE7, SquareE6, Pawn, None } },
    { 0x728c8c6c5a2a8d7f, { SquareD5, SquareC4, Pawn, None } },
    { 0x771d5c9940d94135, { SquareB7, SquareB5, Pawn, None } },
    { 0x772340c0476113f2, { SquareF1, SquareE1, Rook, None } },
    { 0x7b468c3c52f0e687, { SquareB8, SquareC6, Knight, None } },
    { 0x7b5189662e682bf4, { SquareB1, SquareC3, Knight, None } },
    { 0x7b5189662e682bf4, { SquareG1, SquareF3, Knight, None } },
    { 0x7b5189662e682bf4, { SquareG2, SquareG3, Pawn, None } },
    { 0x7e66bc86db85e191, { SquareC2, SquareC3, Pawn, None } },
    { 0x7fa74537f644b686, { SquareC1, SquareD2, Bishop, None } },
    { 0x7fa74537f644b686, { SquareB1, SquareC3, Knight, None } },
    { 0x7fa74537f644b686, { SquareB1, SquareD2, Knight, None } },
    { 0x806dda42529f51d7, { SquareD4, SquareE5, Pawn, None } },
    { 0x8113709fbde8b533, { SquareE2, SquareE4, Pawn, None } },
    { 0x81892c92a42537ca, { SquareG1, SquareF3, Knight, None } },
    { 0x835e19eb89695427, { SquareG8, SquareF6, Knight, None } },
    { 0x837e797d79c302a7, { SquareG8, SquareF6, Knight, None } },
    { 0x85309a8981e27b8c, { SquareD1, SquareD4, Queen, None } },
    { 0x8bff6f9173cc4f22, { SquareB1, SquareC3, Knight, None } },
    { 0x8bff6f9173cc4f22, { SquareA2, SquareA3, Pawn, None } },
    { 0x8bff6f9173cc4f22, { SquareG2, SquareG3, Pawn, None } },
    { 0x8dbbb800beb1702f, { SquareF2, SquareF3, Pawn, None } },
    { 0x8dbbb800beb1702f, { SquareH2, SquareH3, Pawn, None } },
    { 0x8dbbb800beb1702f, { SquareG1, SquareF3, Knight, None } },
    { 0x8f48c14533191d33, { SquareB8, SquareC6, Knight, None } },
    { 0x9142656b167f6376, { SquareB7, SquareC6, Pawn, None } },
    { 0x928f2c176f4fb05e, { SquareF3, SquareD4, Knight, None } },
    { 0x932172663694274b, { SquareC8, SquareF5, Bishop, None } },
    { 0x944cdd4e3c1231e3, { SquareG2, SquareG3, Pawn, None } },
    { 0x9596bcedff61e9ef, { SquareG7, SquareG6, Pawn, None } },
    { 0x9596bcedff61e9ef, { SquareE7, SquareE6, Pawn, None } },
    { 0x9596bcedff61e9ef, { SquareC7, SquareC5, Pawn, None } },
    { 0x96596f8ffdf84252, { SquareG1, SquareF3, Knight, None } },
    { 0x9b0cfbe7bfbd6b82, { SquareE8, SquareG8, King, None } },
    { 0x9c6dcadc0f502d7c, { SquareF7, SquareF5, Pawn, None } },
    { 0x9c6dcadc0f502d7c, { SquareE7, SquareE6, Pawn, None } },
    { 0x9c6dcadc0f502d7c, { SquareG8, SquareF6, Knight, None } },
    { 0x9c6dcadc0f502d7c, { SquareD7, SquareD5, Pawn, None } },
    { 0x9e1254ed5b783746, { SquareC2, SquareC4, Pawn, None } },
    { 0x9e1254ed5b783746, { SquareC1, SquareF4, Bishop, None } },
    { 0x9e1254ed5b783746, { SquareC1, SquareG5, Bishop, None } },
    { 0xa14bb475018ad44e, { SquareG1, SquareF3, Knight, None } },
    { 0xa14bb475018ad44e, { SquareD4, SquareD5, Pawn, None } },
    { 0xa1502a0d1ea24c9f, { SquareG2, SquareG3, Pawn, None } },
    { 0xa50e7a918d86c066, { SquareG1, SquareF3, Knight, None } },
    { 0xa50e7a918d86c066, { SquareC4, SquareD5, Pawn, None } },
    { 0xa5f9c801e7fa8b06, { SquareB1, SquareD2, Knight, None } },
    { 0xa5f9c801e7fa8b06, { SquareG1, SquareF3, Knight, None } },
    { 0xa5f9c801e7fa8b06, { SquareC4, SquareB5, Pawn, None } },
    { 0xa5f9c801e7fa8b06, { SquareD1, SquareC2, Queen, None } },
    { 0xa6783e4881424e75, { SquareD2, SquareD4, Pawn, None } },
    { 0xa715714138195876, { SquareB1, SquareC3, Knight, None } },
    { 0xa7c23ee3c047cd1a, { SquareD7, SquareD5, Pawn, None } },
    { 0xa8e0763a04a10e93, { SquareG7, SquareG6, Pawn, None } },
    { 0xa8eb83c08141fcdb, { SquareD7, SquareD6, Pawn, None } },
    { 0xad569312fef7c715, { SquareA2, SquareA4, Pawn, None } },
    { 0xad6fa69898634ef3, { SquareD1, SquareB3, Queen, None } },
    { 0xad6fa69898634ef3, { SquareC1, SquareG5, Bishop, None } },
    { 0xae1735c4649bc76b, { SquareB1, SquareC3, Knight, None } },
    { 0xae8af5dd4eb855ce, { SquareC8, SquareA6, Bishop, None } },
    { 0xae8af5dd4eb855ce, { SquareC8, SquareB7, Bishop, None } },
    { 0xb09eac6d1f057cf2, { SquareB1, SquareC3, Knight, None } },
    { 0xb0add3ec9c660c26, { SquareG1, SquareF3, Knight, None } },
    { 0xb26e82ea7ef3cc86, { SquareC1, SquareF4, Bishop, None } },
    { 0xb2e313c63b84c00b, { SquareF6, SquareD5, Knight, None } },
    { 0xb4b17ec55ad37075, { SquareC5, SquareB6, Bishop, None } },
    { 0xb8fef8a0e92a799d, { SquareB1, SquareC3, Knight, None } },
    { 0xb970d6e6e824aaad, { SquareB7, SquareB6, Pawn, None } },
    { 0xb970d6e6e824aaad, { SquareF8, SquareB4, Bishop, None } },
    { 0xb9868bd526ff3608, { SquareB1, SquareC3, Knight, None } },
    { 0xba32bcd2a4eb74e1, { SquareG8, SquareF6, Knight, None } },
    { 0xba32bcd2a4eb74e1, { SquareF8, SquareB4, Bishop, None } },
    { 0xbc3acf1b5df7f379, { SquareC3, SquareE4, Knight, None } },
    { 0xbe510e7ff832ceea, { SquareE4, SquareE5, Pawn, None } },
    { 0xc08790e62dc60113, { SquareG8, SquareF6, Knight, None } },
    { 0xc1e43f5fe77c901b, { SquareE7, SquareE5, Pawn, None } },
    { 0xc317a4b4376319bd, { SquareC6, SquareD5, Pawn, None } },
    { 0xc3a78b06026cca60, { SquareD7, SquareD5, Pawn, None } },
    { 0xc681e0f36577f3a4, { SquareG1, SquareF3, Knight, None } },
    { 0xc681e0f36577f3a4, { SquareC2, SquareC3, Pawn, None } },
    { 0xc7f018ef4d46e0ad, { SquareG2, SquareG3, Pawn, None } },
    { 0xc9d19d1955597468, { SquareD4, SquareC6, Knight, None } },
    { 0xc9d19d1955597468, { SquareD4, SquareB3, Knight, None } },
    { 0xd369b5e06daa50e9, { SquareD2, SquareD4, Pawn, None } },
    { 0xd4a219f0edb6b299, { SquareC1, SquareG5, Bishop, None } },
    { 0xd9f2e34ad83a3213, { SquareF8, SquareG7, Bishop, None } },
    { 0xd9f841549cfb38f6, { SquareD2, SquareD4, Pawn, None } },
    { 0xd9f841549cfb38f6, { SquareC2, SquareC3, Pawn, None } },
    { 0xd9f841549cfb38f6, { SquareE1, SquareG1, King, None } },
    { 0xdb1b7334d7290b53, { SquareE5, SquareD4, Pawn, None } },
    { 0xdc96a1beedae97c3, { SquareG1, SquareF3, Knight, None } },
    { 0xdcfe27c837ae0afe, { SquareG8, SquareF6, Knight, None } },
    { 0xdd60b658be7c959c, { SquareD2, SquareD4, Pawn, None } },
    { 0xde3d76ced53bca5e, { SquareG8, SquareF6, Knight, None } },
    { 0xdfbd167ac7be9ace, { SquareF1, SquareD3, Bishop, None } },
    { 0xe413b9c3b47634d4, { SquareF8, SquareB4, Bishop, None } },
    { 0xe515765f43ce6d21, { SquareD7, SquareD5, Pawn, None } },
    { 0xe569f3a3f43a0a20, { SquareC2, SquareC3, Pawn, None } },
    { 0xe569f3a3f43a0a20, { SquareD2, SquareD3, Pawn, None } },
    { 0xe569f3a3f43a0a20, { SquareE1, SquareG1, King, None } },
    { 0xe81ccc00dbd5d9f3, { SquareE2, SquareE4, Pawn, None } },
    { 0xe81ccc00dbd5d9f3, { SquareG1, SquareF3, Knight, None } },
    { 0xec40b586a574fd05, { SquareB8, SquareC6, Knight, None } },
    { 0xedcebcc9c034925f, { SquareE2, SquareE3, Pawn, None } },
    { 0xedeedc5f309ec4df, { SquareE1, SquareG1, King, None } },
    { 0xeebb5a520a5d451b, { SquareC8, SquareB7, Bishop, None } },
    { 0xf00a1015e4937bfb, { SquareE2, SquareE4, Pawn, None } },
    { 0xf2f6193490ea0d27, { SquareD5, SquareD4, Pawn, None } },
    { 0xf37c54cce857f5e8, { SquareF2, SquareF3, Pawn, None } },
    { 0xf37c54cce857f5e8, { SquareE4, SquareE5, Pawn, None } },
    { 0xf37c54cce857f5e8, { SquareE4, SquareD5, Pawn, None } },
    { 0xf37c54cce857f5e8, { SquareB1, SquareC3, Knight, None } },
    { 0xf8c9caadb4a5842a, { SquareG8, SquareF6, Knight, None } },
    { 0xf8f4d9e91f79c779, { SquareA7, SquareA6, Pawn, None } },
    { 0xf8f4d9e91f79c779, { SquareF7, SquareF5, Pawn, None } },
    { 0xf8f4d9e91f79c779, { SquareG8, SquareF6, Knight, None } },
    { 0xfade1dae36494325, { SquareB7, SquareB5, Pawn, None } },
    { 0xfb9ca5c384f29715, { SquareG1, SquareF3, Knight, None } },
    { 0xfb9ca5c384f29715, { SquareB1, SquareC3, Knight, None } },
    { 0xfe9077bbcfb3113b, { SquareE5, SquareF4, Pawn, None } },
    { 0xffff176ed82b2d1e, { SquareG7, SquareG6, Pawn, None } }
};

static int OpeningCompare(const void * a, const void * b)
{
    return (int)(((Opening *) a)->hash - ((Opening *) b)->hash);
}

static size_t FindMatchingOpeningIndex(const Board * board)
{
    const Opening * current;
    size_t lower = 0;
    size_t upper = NUM_OPENINGS;
    size_t i;
    while (lower < upper)
    {
        i = (lower + upper) >> 1; // Bit shift is equal to division by 2.
        current = &s_openingBook[i];

        if (board->hash == current->hash)
            return i;
        else if (board->hash > current->hash)
            lower = i + 1;
        else
            upper = i;
    }
    return NUM_OPENINGS;
}

bool OpeningBookFind(const Board * board, Move * move)
{
    // Quick shortcut: don't bother searching for openings if we are far enough along in the game.
    if (board->ply > MAX_OPENING_BOOK_PLY)
        return false;

    // Binary search to find a matching opening, then look forward and backward (iterate)
    // to find alterative moves for the same position.
    size_t index = FindMatchingOpeningIndex(board);
    if (index >= NUM_OPENINGS)
        return false;

    size_t upper = index;
    size_t lower = index;

    size_t i;
    for (i = index + 1; i < NUM_OPENINGS; ++i)
    {
        if (s_openingBook[i].hash == s_openingBook[index].hash)
            upper = i;
        else
            break;
    }

    i = index;
    while (i-- > 0)
    {
        if (s_openingBook[i].hash == s_openingBook[index].hash)
            lower = i;
        else
            break;
    }

    if (upper == lower)
    {
        *move = s_openingBook[upper].nextMove;
    }
    else
    {
        // Multiple options are available. Pick one at random. For this, choose a proper
        // pseudorandom number rather than the "fake" one used for Zobrist hashing, which is very deterministic.
        assert(upper > lower);
        size_t numOptions = upper - lower;
        size_t option = lower + (size_t)(rand() % numOptions);
        *move = s_openingBook[option].nextMove;
    }

    return true;
}
