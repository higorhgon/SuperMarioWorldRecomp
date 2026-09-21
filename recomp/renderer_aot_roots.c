#include "cpu_state.h"
#include "funcs.h"

/* Host-call roots for the generated ownership and gameplay policy hooks.
 * tools/apply_renderer_hooks.py verifies these sites after every regeneration.
 * This manifest is scanned by the generator, never compiled into the game. */
void SmwRendererAotRoots(CpuState *cpu) {
  ProcessNormalSprites(cpu);
  ProcessExtendedSprites(cpu);
  ExtSpr0D_Baseball(cpu);
  GenericExtendedSpriteGFXRt_FireballEntry(cpu);
  FinishOAMWrite_01B844(cpu);
  GetDrawInfo_Bank01_Recomp(cpu);
  GetDrawInfo_Bank23_Recomp(cpu);
  DrawWingTiles_ParaKoopaEntry(cpu);
  GenericGFXRtDraw1Tile16x16_019F0F(cpu);
  Spr05F_BrownChainedPlatform(cpu);
  sub_1C9EC(cpu);
  SubOffscreen_Bank01_Entry4(cpu);
  SubOffscreen_Bank01_Entry3(cpu);
  SubOffscreen_Bank01_Entry2(cpu);
  SubOffscreen_Bank01_01AC2D(cpu);
  SubOffscreen_Bank01_Entry1(cpu);
  SubOffscreen_Bank01_01AC33(cpu);
  SubOffscreen_Bank02_Entry4(cpu);
  SubOffscreen_Bank02_Entry3(cpu);
  SubOffscreen_Bank02_Entry2(cpu);
  SubOffscreen_Bank02_Entry1(cpu);
  SubOffscreen_Bank03_Entry4(cpu);
  SubOffscreen_Bank03_Entry3(cpu);
  SubOffscreen_Bank03_Entry2(cpu);
  SubOffscreen_Bank03_Entry1(cpu);
  SubOffscreen_Bank03_03B85F(cpu);
  ParseLevelSpriteList(cpu);
  ParseLevelSpriteList_Entry2(cpu);
  ProcessNormalSprites_GetNormalSpriteOAMIndexAndDecrementTimers(cpu);
  Spr0DE_Load5Eeries(cpu);
}
