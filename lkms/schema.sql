-- LKMS: private, central licence key management. PostgreSQL 14+.
-- Secrets are never stored in the clear: app secrets and licence keys are kept as hashes, device ids as hashes.
-- Apply with:  psql "$LKMS_DATABASE_URL" -f schema.sql   (safe to run again: everything is IF NOT EXISTS)

CREATE EXTENSION IF NOT EXISTS pgcrypto;   -- gen_random_uuid()

-- One row per client application that may ask about licences.
CREATE TABLE IF NOT EXISTS apps (
    app_id           uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    name             text NOT NULL UNIQUE CHECK (length(name) BETWEEN 1 AND 120),
    api_secret_hash  text NOT NULL,                          -- SHA-256 of the app's secret (hex); the secret is shown once, at creation
    status           text NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'disabled')),
    created_at       timestamptz NOT NULL DEFAULT now()
);

-- One row per licence key. The key itself is not stored: key_hash is HMAC-SHA256(server pepper, key).
CREATE TABLE IF NOT EXISTS licenses (
    license_id       uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    key_hash         text NOT NULL UNIQUE,
    key_hint         text NOT NULL,                          -- first group of the key, for support ("ABCDE-…")
    app_id           uuid NOT NULL REFERENCES apps(app_id) ON DELETE RESTRICT,
    customer_email   text CHECK (customer_email IS NULL OR customer_email ~* '^[^@\s]+@[^@\s]+\.[^@\s]+$'),
    plan             text NOT NULL DEFAULT 'standard' CHECK (length(plan) BETWEEN 1 AND 60),
    max_activations  integer NOT NULL DEFAULT 1 CHECK (max_activations BETWEEN 1 AND 100000),
    expires_at       timestamptz,                            -- NULL: never expires
    status           text NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'suspended', 'revoked', 'expired')),
    status_reason    text,
    metadata         jsonb NOT NULL DEFAULT '{}'::jsonb,
    -- 'device': each device asks the central server. 'volume': a site host on the customer's network holds the licence
    -- and hands out volume_seats seats to the devices there; max_activations is then the number of site hosts allowed.
    kind             text NOT NULL DEFAULT 'device' CHECK (kind IN ('device', 'volume')),
    volume_seats     integer CHECK (volume_seats IS NULL OR volume_seats BETWEEN 1 AND 1000000),
    created_at       timestamptz NOT NULL DEFAULT now(),
    updated_at       timestamptz NOT NULL DEFAULT now(),
    CHECK ((kind = 'volume') = (volume_seats IS NOT NULL))
);
-- databases made before volume licences existed
ALTER TABLE licenses ADD COLUMN IF NOT EXISTS kind text NOT NULL DEFAULT 'device' CHECK (kind IN ('device', 'volume'));
ALTER TABLE licenses ADD COLUMN IF NOT EXISTS volume_seats integer CHECK (volume_seats IS NULL OR volume_seats BETWEEN 1 AND 1000000);
CREATE INDEX IF NOT EXISTS licenses_app_idx      ON licenses (app_id, status);
CREATE INDEX IF NOT EXISTS licenses_email_idx    ON licenses (lower(customer_email));
CREATE INDEX IF NOT EXISTS licenses_expires_idx  ON licenses (expires_at) WHERE expires_at IS NOT NULL;
CREATE INDEX IF NOT EXISTS licenses_metadata_idx ON licenses USING gin (metadata);

-- One row per device that holds a seat of a licence. A device can hold one seat per licence.
CREATE TABLE IF NOT EXISTS activations (
    activation_id    uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    license_id       uuid NOT NULL REFERENCES licenses(license_id) ON DELETE CASCADE,
    device_hash      text NOT NULL,                          -- SHA-256 of the device id the app sent (the raw id is not kept)
    device_name      text,
    ip_address       inet,
    activated_at     timestamptz NOT NULL DEFAULT now(),
    last_seen_at     timestamptz NOT NULL DEFAULT now(),
    UNIQUE (license_id, device_hash)
);
CREATE INDEX IF NOT EXISTS activations_license_idx ON activations (license_id);
CREATE INDEX IF NOT EXISTS activations_seen_idx    ON activations (last_seen_at);

-- Replay protection: a (app, nonce) pair is accepted once. Old rows are deleted by the service.
CREATE TABLE IF NOT EXISTS request_nonces (
    app_id           uuid NOT NULL REFERENCES apps(app_id) ON DELETE CASCADE,
    nonce            text NOT NULL,
    seen_at          timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (app_id, nonce)
);
CREATE INDEX IF NOT EXISTS request_nonces_seen_idx ON request_nonces (seen_at);

-- What happened, for support and for spotting abuse. No secrets and no raw keys are written here.
CREATE TABLE IF NOT EXISTS audit_events (
    event_id         bigserial PRIMARY KEY,
    at               timestamptz NOT NULL DEFAULT now(),
    actor            text NOT NULL,                          -- 'admin' or 'app:<app_id>'
    action           text NOT NULL,
    license_id       uuid,
    app_id           uuid,
    ip_address       inet,
    detail           jsonb NOT NULL DEFAULT '{}'::jsonb
);
CREATE INDEX IF NOT EXISTS audit_license_idx ON audit_events (license_id, at DESC);
CREATE INDEX IF NOT EXISTS audit_at_idx      ON audit_events (at DESC);
