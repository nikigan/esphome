import esphome.codegen as cg
from esphome.components import climate_ir

AUTO_LOAD = ["climate_ir"]
CODEOWNERS = ["@nikigan"]

aux_ns = cg.esphome_ns.namespace("aux")
AuxClimate = aux_ns.class_("AuxClimate", climate_ir.ClimateIR)

CONFIG_SCHEMA = climate_ir.climate_ir_with_receiver_schema(AuxClimate)


async def to_code(config):
    await climate_ir.new_climate_ir(config)
