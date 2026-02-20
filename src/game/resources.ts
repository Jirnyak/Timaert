// Universal resource model for economic simulation
export type ResourceType =
  | 'iron'
  | 'fertility'
  | 'clay'
  | 'wood'
  | 'gold'
  | 'water'
  | 'coal'
  | 'gems'
  | 'fur';

export interface Resource {
  type: ResourceType;
  amount: number;
}

export interface Cell {
  x: number;
  y: number;
  terrain: string;
  resources: Resource[];
  ownerFaction?: string;
}

export interface Landmark {
  id: string;
  type: 'village' | 'city';
  location: { x: number; y: number };
  population: number;
  inventory: Record<string, number>;
  productionCapacity: number;
  ownerFaction?: string;
}

// Extensible for new resources, landmarks, etc.
