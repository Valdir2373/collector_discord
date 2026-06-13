#pragma once
#include "pch.h"

// Inicia o popup principal (z-order acima de tudo)
// Busca a imagem da API em memória via WinHTTP — sem salvar no disco
// Se sem internet, exibe o popup sem imagem
// Deve ser chamado em std::thread separado
void QrPopupThread();
