// renderer_host: selecao do backend grafico em runtime (dxvk | native-render).
// Ver o .cpp para a ordem de selecao. Chamado apenas pela bridge D3D9.
#pragma once
#include <d3d9.h>

IDirect3D9 *renderer_host_direct3d9(UINT sdk_version);
