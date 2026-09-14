/* SPDX-License-Identifier: MIT */

#ifndef ARDUBOY3D_SPRITE_FRAME_PREFIX
#error "ARDUBOY3D_SPRITE_FRAME_PREFIX must be defined before including this file"
#endif

#define ARDUBOY3D_CAT2(a, b) a##b
#define ARDUBOY3D_CAT(a, b) ARDUBOY3D_CAT2(a, b)

#define skeletonSpriteData_numFrames \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _skeletonSpriteData_numFrames)
#define mageSpriteData_numFrames \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _mageSpriteData_numFrames)
#define torchSpriteData1_numFrames \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _torchSpriteData1_numFrames)
#define torchSpriteData2_numFrames \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _torchSpriteData2_numFrames)
#define projectileSpriteData_numFrames \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _projectileSpriteData_numFrames)
#define enemyProjectileSpriteData_numFrames \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _enemyProjectileSpriteData_numFrames)
#define entranceSpriteData_numFrames \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _entranceSpriteData_numFrames)
#define exitSpriteData_numFrames \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _exitSpriteData_numFrames)
#define urnSpriteData_numFrames \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _urnSpriteData_numFrames)
#define signSpriteData_numFrames \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _signSpriteData_numFrames)
#define crownSpriteData_numFrames \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _crownSpriteData_numFrames)
#define coinsSpriteData_numFrames \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _coinsSpriteData_numFrames)
#define scrollSpriteData_numFrames \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _scrollSpriteData_numFrames)
#define chestSpriteData_numFrames \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _chestSpriteData_numFrames)
#define chestOpenSpriteData_numFrames \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _chestOpenSpriteData_numFrames)
#define potionSpriteData_numFrames \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _potionSpriteData_numFrames)
#define batSpriteData_numFrames \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _batSpriteData_numFrames)
#define spiderSpriteData_numFrames \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _spiderSpriteData_numFrames)
#define wallTextureData_numTextures \
	ARDUBOY3D_CAT(ARDUBOY3D_SPRITE_FRAME_PREFIX, _wallTextureData_numTextures)
