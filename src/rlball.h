#pragma once
#include "planets.h"

// Superficie del balon de Rocket League, generada en codigo y horneada en un
// cubemap. La usan el planeta Arena (visto desde el espacio) y el balon del
// mundo abierto. El diseno sale de un cubo inflado:
//   - caras +-X y +-Y: paneles blancos con un relieve suave de hexagonos grandes;
//   - caras +-Z: paneles grises con una malla fina de hexagonos (cada uno
//     queda rodeado de paneles blancos, como en el balon real);
//   - aristas del cubo: costuras negras de hule;
//   - esquinas del cubo (donde se juntan tres costuras): placas mecanicas
//     gris claro con estrias, brazos sobre las costuras y un lente negro con
//     aro en el centro.
// tex.color guarda el color y tex.mask el brillo especular de cada parte.
void bakeRocketBall(PlanetTextures& tex, int size);

// Ilumina un punto de esa superficie (color y brillo leidos del cubemap):
// Lambert, Blinn-Phong segun el material y un borde Fresnel azulado.
// ambient: luz minima en el lado de noche.
Vec3 shadeRocketBall(const PlanetTextures& tex, const Vec3& localDir, const Vec3& normal, const Vec3& toLight,
                     const Vec3& toCamera, float ambient);
