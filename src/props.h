#pragma once
#include <functional>
#include <vector>
#include "mesh.h"
#include "noise.h"

// Objetos que se colocan sobre los planetas. Los modelos se construyen en
// codigo con caras planas (estilo low poly). Todos tienen la base en y = 0,
// crecen hacia +Y y estan en unidades del mundo (el carro mide ~1.4).
enum PropType { PROP_ROCK, PROP_TREE, PROP_CRYSTAL, PROP_BEACON };

Mesh makeRock(unsigned seed);  // icosfera deformada con ruido y aplastada
Mesh makeAlienTree();          // tronco hexagonal y tres copas apiladas
Mesh makeCrystalCluster();     // tres bipiramides hexagonales inclinadas
Mesh makeBeacon();             // base, mastil con travesanos y una luz arriba

// Una copia de un modelo sobre un planeta, en el espacio local del planeta.
struct PropInstance {
    const Mesh* model;
    Vec3 direction; // punto de la esfera unitaria donde esta apoyado
    float yaw;      // giro alrededor de la vertical (radianes)
    float scale;
};

// Regla para repartir un tipo de objeto sobre un planeta.
struct ScatterRule {
    std::vector<const Mesh*> models; // variantes del mismo tipo (se elige una al azar)
    int candidates;                  // cuantos puntos al azar se prueban
    float threshold;                 // densidad minima para aceptar un punto (0..1)
    float minSpacing;                // distancia angular minima a otro objeto (radianes)
    float minScale, maxScale;
    // Que tan apto es el terreno en ese punto (0 = prohibido, 1 = ideal).
    std::function<float(const Vec3&)> suitability;
};

// Reparte objetos con una regla. Los puntos candidatos salen de la semilla y
// el ruido de densidad decide cuales se quedan y de que tamano: con la misma
// semilla siempre se obtienen las mismas posiciones.
void scatterProps(const ScatterRule& rule, unsigned seed, const Noise& density, std::vector<PropInstance>& out);

// Junta todas las instancias en una sola malla en el espacio local del
// planeta (sin escala): cada una se apoya en la superficie con su "arriba"
// igual a la vertical de ese punto. Asi se dibujan todas con una sola llamada
// y giran solas con el planeta. shape (opcional) da la altura real del
// terreno si el planeta esta deformado (1 = esfera).
Mesh mergeProps(const std::vector<PropInstance>& instances, float planetRadius,
                const std::function<float(const Vec3&)>& shape = nullptr);
