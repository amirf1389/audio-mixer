FROM python:3.13-slim
WORKDIR /srv
RUN pip install --no-cache-dir fastapi "uvicorn[standard]" cryptography
COPY sitehost.py .
RUN useradd --system --no-create-home lkms && mkdir /data && chown lkms /data
USER lkms
ENV LKMS_HOST_DATA=/data
# Inside the container the server must listen on the container's interface so Docker can publish it; WHICH host address it is published
# on is decided by docker-compose.yml (127.0.0.1 by default). Outside Docker, sitehost.py binds 127.0.0.1 unless LKMS_HOST_BIND says otherwise.
ENV LKMS_HOST_BIND=0.0.0.0
EXPOSE 8090
CMD ["python", "sitehost.py"]
