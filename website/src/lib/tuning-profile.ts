export const DEFAULT_TUNING_PROFILE_ID = 'builtin-default-v1';

export type TuningParameterType = 'integer' | 'boolean';
export type TuningParameterCategory = 'search' | 'time' | 'evaluation' | 'opening';
export type RestartRequirement = 'none' | 'new-search' | 'new-game' | 'engine-restart';

export interface TuningParameterDefinition<T extends number | boolean = number | boolean> {
  name: string;
  type: TuningParameterType;
  defaultValue: T;
  minimum?: number;
  maximum?: number;
  step?: number;
  category: TuningParameterCategory;
  restartRequirement: RestartRequirement;
}

export const TUNING_PARAMETER_DEFINITIONS: readonly TuningParameterDefinition[] = Object.freeze([
  Object.freeze({ name: 'search.aspirationWindowCp', type: 'integer', defaultValue: 75, minimum: 0, maximum: 500, step: 1, category: 'search', restartRequirement: 'new-search' }),
  Object.freeze({ name: 'search.nullMoveReduction', type: 'integer', defaultValue: 2, minimum: 0, maximum: 6, step: 1, category: 'search', restartRequirement: 'new-search' }),
  Object.freeze({ name: 'search.deltaPruningMarginCp', type: 'integer', defaultValue: 260, minimum: 0, maximum: 1000, step: 1, category: 'search', restartRequirement: 'new-search' }),
  Object.freeze({ name: 'time.safetyReserveMs', type: 'integer', defaultValue: 30, minimum: 0, maximum: 1000, step: 1, category: 'time', restartRequirement: 'new-search' }),
  Object.freeze({ name: 'time.estimatedMovesFloor', type: 'integer', defaultValue: 20, minimum: 1, maximum: 80, step: 1, category: 'time', restartRequirement: 'new-search' }),
  Object.freeze({ name: 'time.estimatedMovesBase', type: 'integer', defaultValue: 40, minimum: 1, maximum: 120, step: 1, category: 'time', restartRequirement: 'new-search' }),
  Object.freeze({ name: 'eval.pawnMg', type: 'integer', defaultValue: 150, minimum: 0, maximum: 500, step: 1, category: 'evaluation', restartRequirement: 'new-search' }),
  Object.freeze({ name: 'eval.knightMg', type: 'integer', defaultValue: 250, minimum: 0, maximum: 1000, step: 1, category: 'evaluation', restartRequirement: 'new-search' }),
  Object.freeze({ name: 'eval.bishopMg', type: 'integer', defaultValue: 284, minimum: 0, maximum: 1000, step: 1, category: 'evaluation', restartRequirement: 'new-search' }),
  Object.freeze({ name: 'eval.rookMg', type: 'integer', defaultValue: 490, minimum: 0, maximum: 1500, step: 1, category: 'evaluation', restartRequirement: 'new-search' }),
  Object.freeze({ name: 'eval.queenMg', type: 'integer', defaultValue: 839, minimum: 0, maximum: 2500, step: 1, category: 'evaluation', restartRequirement: 'new-search' }),
  Object.freeze({ name: 'opening.enabled', type: 'boolean', defaultValue: true, category: 'opening', restartRequirement: 'new-game' }),
  Object.freeze({ name: 'opening.bookDepth', type: 'integer', defaultValue: 30, minimum: 0, maximum: 100, step: 1, category: 'opening', restartRequirement: 'new-game' }),
  Object.freeze({ name: 'opening.bookSelectionTopN', type: 'integer', defaultValue: 4, minimum: 1, maximum: 32, step: 1, category: 'opening', restartRequirement: 'new-game' }),
  Object.freeze({ name: 'opening.bookSeed', type: 'integer', defaultValue: 1592594996, minimum: 0, maximum: 2147483647, step: 1, category: 'opening', restartRequirement: 'new-game' }),
]);

export function validateTuningParameterValue(definition: TuningParameterDefinition, value: unknown): boolean {
  if (definition.type === 'boolean') {
    return typeof value === 'boolean';
  }
  if (typeof value !== 'number' || !Number.isFinite(value) || !Number.isInteger(value)) {
    return false;
  }
  if (definition.minimum !== undefined && value < definition.minimum) return false;
  if (definition.maximum !== undefined && value > definition.maximum) return false;
  if (definition.step !== undefined && definition.minimum !== undefined) {
    return (value - definition.minimum) % definition.step === 0;
  }
  return true;
}

