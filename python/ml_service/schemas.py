"""
Pydantic schemas for the KAIROS ML Threat Detection Service API.
See Technical Specification — Module 3, Section 5.2 / 5.3 / 5.6.

NOTE (Section 5.6): `failed_logins` is included per Resolution Option A —
the Gateway tracks failed login attempts per session and sends the count
here so BRUTE_FORCE detection is possible.
"""

from typing import Optional
from pydantic import BaseModel, Field, field_validator


class ThreatFeatures(BaseModel):
    data_length: int = Field(..., ge=0, le=65535, description="Plaintext size in bytes")
    protocol: str = Field(..., description="Transport protocol: websocket|tcp|udp|icmp")
    packet_rate: float = Field(..., ge=0, description="Packets per second (rolling window)")
    avg_packet_size: float = Field(..., ge=0, le=65535, description="Running average packet size")
    destination_diversity: int = Field(..., ge=0, le=255, description="Unique target device count")
    failed_logins: int = Field(0, ge=0, description="Failed login attempts (Section 5.6, Option A)")
    timestamp: Optional[str] = None

    @field_validator("protocol")
    @classmethod
    def validate_protocol(cls, v: str) -> str:
        allowed = {"websocket", "tcp", "udp", "icmp"}
        if v.lower() not in allowed:
            raise ValueError(f"protocol must be one of {sorted(allowed)}, got '{v}'")
        return v.lower()


class ThreatRequest(BaseModel):
    session_id: str = Field(..., min_length=1)
    source: str = Field(..., min_length=1, description="Device ID of sender")
    target: str = Field(..., min_length=1, description="Device ID of intended recipient")
    plaintext: str = Field(..., description="Decrypted plaintext (metadata use only; never logged)")
    features: ThreatFeatures


class ThreatResponse(BaseModel):
    threat_score: float = Field(..., ge=0.0, le=1.0)
    level: str
    confidence: float = Field(..., ge=0.0, le=1.0)
    attack_label: Optional[str] = None
    timestamp: str


class ErrorDetails(BaseModel):
    field: Optional[str] = None
    error: Optional[str] = None


class ErrorResponse(BaseModel):
    status: str = "error"
    error_code: str
    message: str
    timestamp: str
    details: Optional[dict] = None


class HealthResponse(BaseModel):
    status: str
    version: str
    model_loaded: bool
    uptime_seconds: Optional[float] = None
    timestamp: str
    error: Optional[str] = None
