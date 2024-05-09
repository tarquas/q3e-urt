#include "server.h"

int SVMP_ClientThink(client_t *cl) {
  char c[3] = "\0\0", d;

  sharedEntity_t    *ent;
  ent = SV_GentityNum(cl - svs.clients);
Com_Printf("ent: %016x %016x\n", ent->s.eFlags, ent->s.eType);
  d = cl->lastUsercmd.forwardmove;
  if (d < 0) { *c = 'v'; ent->s.pos.trBase[1] -= 1.0f; }
  else if (d > 0) { *c = '^'; ent->s.pos.trBase[1] += 1.0f; }
  c[1] = c[0];
  d = cl->lastUsercmd.rightmove;
  if (d < 0) { *c = '<'; ent->s.pos.trBase[0] -= 1.0f; }
  else if (d > 0) { *c = '>'; ent->s.pos.trBase[0] += 1.0f; }
  if (*c) SV_SendServerCommand(cl, "cp \"%s\"", c);
  //SV_SendServerCommand(cl, "cs 1001 \"^4Blue \r\r\r\r1. Hello\t\t.\r2. Keep\t\t.\r3. Goodbye\r4. ???\t\t\t.\r...\r5. PROFIT\"");
  return 1;
}
