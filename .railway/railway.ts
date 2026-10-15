import { defineRailway, project, service } from "railway/iac";

export default defineRailway(() => {
  const mountain = service("public-mountain", {
    // No GitHub source: releases are manual `railway up` uploads for now.
    build: { builder: "DOCKERFILE", dockerfilePath: "Dockerfile" },
    healthcheck: "/health",
    healthcheckTimeout: 60,
    replicas: { "us-east4-eqdc4a": 1 }, // US East (Virginia), one shared mountain.
    deploy: {
      sleepApplication: false,
      restartPolicyType: "ON_FAILURE",
      restartPolicyMaxRetries: 10,
    },
    env: {
      NODE_ENV: "production",
      RAILWAY_DEPLOYMENT_OVERLAP_SECONDS: "0",
      RAILWAY_DEPLOYMENT_DRAINING_SECONDS: "5",
    },
    // Keep Docker's exec-form CMD: Node must receive SIGTERM directly as PID 1.
  });

  return project("skifree-multiplayer", { resources: [mountain] });
});
