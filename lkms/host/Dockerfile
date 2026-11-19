FROM python:3.13-slim
WORKDIR /srv
RUN pip install --no-cache-dir fastapi "uvicorn[standard]" cryptography
COPY sitehost.py .
RUN useradd --system --no-create-home lkms && mkdir /data && chown lkms /data
USER lkms
ENV LKMS_HOST_DATA=/data
EXPOSE 8090
CMD ["uvicorn", "--factory", "sitehost:app", "--host", "0.0.0.0", "--port", "8090", "--no-server-header"]
