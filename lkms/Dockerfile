FROM python:3.13-slim
WORKDIR /srv
COPY requirements.txt .
RUN pip install --no-cache-dir fastapi "uvicorn[standard]" "psycopg[binary]" psycopg_pool cryptography
COPY lkms ./lkms
RUN useradd --system --no-create-home lkms
USER lkms
EXPOSE 8088
# one process keeps the in-memory rate limits exact; put more behind a proxy that limits too, if you ever need them
CMD ["uvicorn", "--factory", "lkms.main:app", "--host", "0.0.0.0", "--port", "8088", "--no-server-header"]
