pipeline {
  agent none
  stages {
    stage('Configure') {
      agent {
        node {
          label 'linux'
        }

      }
      steps {
        sh './bootstrap'
        sh './configure'
      }
    }
    stage('Build backup client') {
      agent {
        node {
          label 'linux'
        }

      }
      steps {
        sh 'make build-backup-client'
      }
    }
    stage('Build backup server') {
      agent {
        node {
          label 'linux'
        }

      }
      steps {
        sh 'make build-backup-server'
      }
    }
  }
}