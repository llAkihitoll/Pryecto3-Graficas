#pragma once
#include <string>
#include "mesh.h"

// Lee un OBJ con lineas v, vn, f (v, v/vt, v//vn o v/vt/vn), usemtl y
// mtllib. Las caras de mas de 3 vertices se dividen en abanico. Si el
// archivo no trae normales, se calculan promediando las de las caras.
bool loadOBJ(const std::string& path, Mesh& mesh);
